#include "Detector.hpp"
#include "../common/Logger.hpp"

#ifdef OSCAR_HAVE_ONNX
#include <onnxruntime_cxx_api.h>
#endif

#ifdef OSCAR_HAVE_OPENCV
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>

namespace oscar::inference {

class IBackend {
public:
    virtual ~IBackend() = default;
    virtual std::vector<BBox> infer(const Frame& f) = 0;
    virtual const char* name() const = 0;
};

namespace {

std::vector<std::string> load_labels(const std::string& p) {
    std::vector<std::string> out;
    std::ifstream f(p);
    std::string ln;
    while (std::getline(f, ln)) if (!ln.empty()) out.push_back(ln);
    if (out.empty()) {
        out = {"person","bicycle","car","motorcycle","airplane","bus","train","truck","boat","traffic light",
               "fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow"};
    }
    return out;
}

// Stub backend: only fires when the synthetic camera path emits its
// known green square — never invents detections on real frames.
class StubBackend : public IBackend {
public:
    explicit StubBackend(const std::vector<std::string>& labels) : labels_(labels) {}
    const char* name() const override { return "stub"; }

    std::vector<BBox> infer(const Frame& f) override {
        std::vector<BBox> out;
        if (f.data.empty()) return out;
        int W = f.width, H = f.height;
        int minx=W, miny=H, maxx=-1, maxy=-1;
        for (int y = 0; y < H; y += 8) {
            for (int x = 0; x < W; x += 8) {
                size_t i = (size_t(y) * W + x) * 3;
                if (i + 2 >= f.data.size()) continue;
                uint8_t b = f.data[i], g = f.data[i+1], r = f.data[i+2];
                if (g > 150 && g > b + 50 && g > r + 50) {
                    if (x < minx) minx = x;
                    if (y < miny) miny = y;
                    if (x > maxx) maxx = x;
                    if (y > maxy) maxy = y;
                }
            }
        }
        if (maxx > minx && maxy > miny) {
            BBox b;
            b.x = float(minx);
            b.y = float(miny);
            b.w = float(maxx - minx);
            b.h = float(maxy - miny);
            b.score = 0.91f;
            b.class_id = 0;
            b.class_name = labels_.empty() ? "object" : labels_[0];
            out.push_back(b);
        }
        // No synthetic fallback box — real frames stay empty when no model.
        return out;
    }

private:
    std::vector<std::string> labels_;
};

#ifdef OSCAR_HAVE_OPENCV
// YOLOv5 / YOLOv8 ONNX via OpenCV DNN. Auto-detects output layout.
//   YOLOv5: [1, N, 85]        cx,cy,w,h, obj,  80 cls
//   YOLOv8: [1, 84, N]   (no obj; transposed)
class OpenCVDnnBackend : public IBackend {
public:
    OpenCVDnnBackend(const InferenceCfg& cfg, const std::vector<std::string>& labels)
        : cfg_(cfg), labels_(labels), input_size_(cfg.input_size > 0 ? cfg.input_size : 640) {
        net_ = cv::dnn::readNetFromONNX(cfg_.model_path);
        net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    }
    const char* name() const override { return "opencv-dnn"; }

    std::vector<BBox> infer(const Frame& f) override {
        std::vector<BBox> result;
        if (f.data.empty() || f.width <= 0 || f.height <= 0) return result;

        cv::Mat img(f.height, f.width, CV_8UC3, const_cast<uint8_t*>(f.data.data()));

        // letterbox to input_size_ x input_size_
        const int IS = input_size_;
        float r = std::min(float(IS) / f.width, float(IS) / f.height);
        int new_w = int(std::round(f.width  * r));
        int new_h = int(std::round(f.height * r));
        cv::Mat resized;
        cv::resize(img, resized, cv::Size(new_w, new_h));
        cv::Mat padded(IS, IS, CV_8UC3, cv::Scalar(114, 114, 114));
        int dw = (IS - new_w) / 2;
        int dh = (IS - new_h) / 2;
        resized.copyTo(padded(cv::Rect(dw, dh, new_w, new_h)));

        cv::Mat blob;
        cv::dnn::blobFromImage(padded, blob, 1.0/255.0, cv::Size(IS, IS),
                               cv::Scalar(), /*swapRB=*/true, /*crop=*/false, CV_32F);
        net_.setInput(blob);
        cv::Mat out = net_.forward();

        // out shape: [1, A, B]. v5 → A=N, B=85 (or 5+num_cls). v8 → A=4+num_cls, B=N.
        if (out.dims != 3) {
            LOG_WARN("inference", "unexpected output dims=", out.dims);
            return result;
        }
        int A = out.size[1];
        int B = out.size[2];
        int num_classes = (labels_.empty() ? 80 : int(labels_.size()));
        bool is_v8 = (A == 4 + num_classes) && (B > A);

        cv::Mat det;
        if (is_v8) {
            // transpose to [N, 84] for row-major access
            cv::Mat raw(A, B, CV_32F, out.ptr<float>(0));
            cv::transpose(raw, det);
        } else {
            det = cv::Mat(A, B, CV_32F, out.ptr<float>(0));
        }

        std::vector<int>       class_ids;
        std::vector<float>     confs;
        std::vector<cv::Rect>  boxes;
        const int cls_off = is_v8 ? 4 : 5;

        for (int i = 0; i < det.rows; ++i) {
            const float* row = det.ptr<float>(i);
            float obj = is_v8 ? 1.0f : row[4];
            if (obj < 1e-3f) continue;

            int   best_cls   = -1;
            float best_score = 0.f;
            for (int c = 0; c < num_classes; ++c) {
                float sc = row[cls_off + c];
                if (sc > best_score) { best_score = sc; best_cls = c; }
            }
            float conf = obj * best_score;
            if (conf < cfg_.conf_threshold) continue;

            float cx = row[0], cy = row[1], w = row[2], h = row[3];
            // reverse letterbox
            float xmin = (cx - w * 0.5f - dw) / r;
            float ymin = (cy - h * 0.5f - dh) / r;
            float ww   = w / r;
            float hh   = h / r;

            // clip to image
            xmin = std::max(0.f, std::min(xmin, float(f.width  - 1)));
            ymin = std::max(0.f, std::min(ymin, float(f.height - 1)));
            ww = std::max(1.f, std::min(ww, float(f.width)  - xmin));
            hh = std::max(1.f, std::min(hh, float(f.height) - ymin));

            class_ids.push_back(best_cls);
            confs.push_back(conf);
            boxes.emplace_back(int(xmin), int(ymin), int(ww), int(hh));
        }

        std::vector<int> keep;
        cv::dnn::NMSBoxes(boxes, confs, cfg_.conf_threshold, cfg_.iou_threshold, keep);

        for (int idx : keep) {
            BBox b;
            b.x = float(boxes[idx].x);
            b.y = float(boxes[idx].y);
            b.w = float(boxes[idx].width);
            b.h = float(boxes[idx].height);
            b.score = confs[idx];
            b.class_id = class_ids[idx];
            b.class_name = (class_ids[idx] >= 0 && class_ids[idx] < int(labels_.size()))
                ? labels_[class_ids[idx]] : "obj";
            result.push_back(b);
        }
        return result;
    }

private:
    InferenceCfg cfg_;
    std::vector<std::string> labels_;
    cv::dnn::Net net_;
    int input_size_;
};
#endif

#ifdef OSCAR_HAVE_ONNX
class OnnxBackend : public IBackend {
public:
    OnnxBackend(const InferenceCfg& cfg, const std::vector<std::string>& labels)
        : cfg_(cfg), labels_(labels),
          env_(ORT_LOGGING_LEVEL_WARNING, "oscar"),
          session_(env_, cfg.model_path.c_str(), Ort::SessionOptions{nullptr}) {}
    const char* name() const override { return "onnx"; }

    std::vector<BBox> infer(const Frame& f) override {
        // Real preprocessing/postprocessing (NMS) is model-dependent.
        // Left intentionally minimal — production models plug here.
        (void)f;
        return {};
    }
private:
    InferenceCfg cfg_;
    std::vector<std::string> labels_;
    Ort::Env env_;
    Ort::Session session_;
};
#endif

} // namespace

Detector::Detector(InferenceCfg cfg, Pipeline pipe)
    : cfg_(std::move(cfg)), pipe_(std::move(pipe)) {
    labels_ = load_labels(cfg_.labels_path);
    bool model_present = std::filesystem::exists(cfg_.model_path);
    bool use_stub = cfg_.stub_mode || !model_present;

    if (!model_present && !cfg_.stub_mode) {
        LOG_WARN("inference", "model file not found at ", cfg_.model_path,
                              " — falling back to stub (set OSCAR_INFERENCE_MODEL or run 'make download-model')");
    }

#ifdef OSCAR_HAVE_ONNX
    if (!use_stub) {
        try {
            backend_ = std::make_unique<OnnxBackend>(cfg_, labels_);
            LOG_INFO("inference", "ONNX Runtime backend ready model=", cfg_.model_path);
            return;
        } catch (const std::exception& e) {
            LOG_WARN("inference", "ONNX init failed: ", e.what(), " — trying OpenCV DNN");
        }
    }
#endif
#ifdef OSCAR_HAVE_OPENCV
    if (!use_stub) {
        try {
            backend_ = std::make_unique<OpenCVDnnBackend>(cfg_, labels_);
            LOG_INFO("inference", "OpenCV DNN backend ready model=", cfg_.model_path,
                                  " input=", cfg_.input_size);
            return;
        } catch (const std::exception& e) {
            LOG_WARN("inference", "OpenCV DNN init failed: ", e.what(), " -> stub");
            use_stub = true;
        }
    }
#endif
    backend_ = std::make_unique<StubBackend>(labels_);
    LOG_INFO("inference", "Stub backend active",
             (cfg_.stub_mode ? " (stub_mode=true)" : " (no real model loadable)"));
}

Detector::~Detector() { stop(); }

void Detector::start() {
    if (running_.exchange(true)) return;
    th_ = std::thread([this]{ run(); });
}

void Detector::stop() {
    if (!running_.exchange(false)) return;
    if (th_.joinable()) th_.join();
}

void Detector::run() {
    using clk = std::chrono::steady_clock;
    Frame f;
    while (running_.load()) {
        if (!pipe_.frames->pop(f, 100)) continue;
        auto t0 = clk::now();
        DetectionResult dr;
        dr.ts_ms = now_ms();
        dr.frame_seq = f.seq;
        dr.boxes = backend_->infer(f);
        // filter by conf
        std::vector<BBox> kept;
        kept.reserve(dr.boxes.size());
        for (auto& b : dr.boxes) if (b.score >= cfg_.conf_threshold) kept.push_back(b);
        dr.boxes.swap(kept);

        pipe_.detections->push(std::move(dr));

        auto t1 = clk::now();
        float lat = std::chrono::duration<float, std::milli>(t1 - t0).count();
        float prev = avg_lat_ms_.load();
        avg_lat_ms_.store(prev == 0.f ? lat : (prev * 0.9f + lat * 0.1f));
        ++infer_count_;
    }
}

} // namespace oscar::inference
