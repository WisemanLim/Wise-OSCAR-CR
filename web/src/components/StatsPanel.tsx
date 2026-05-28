import { useCallback, useEffect, useState } from 'react';
import {
  CameraDevice,
  getStats,
  listCameras,
  setCameraSource,
  Stats,
} from '../api/oscar';

export function StatsPanel() {
  const [s, setS] = useState<Stats | null>(null);
  const [devices, setDevices] = useState<CameraDevice[]>([]);
  const [current, setCurrent] = useState<string>('');
  const [selected, setSelected] = useState<string>('');
  const [customUrl, setCustomUrl] = useState<string>('');
  const [scanning, setScanning] = useState(false);
  const [applying, setApplying] = useState(false);
  const [msg, setMsg] = useState<string>('');
  const [platform, setPlatform] = useState<string>('unknown');
  const [inContainer, setInContainer] = useState<boolean>(false);

  useEffect(() => {
    const tick = async () => {
      try { setS(await getStats()); } catch { /* ignore */ }
    };
    tick();
    const id = setInterval(tick, 1000);
    return () => clearInterval(id);
  }, []);

  const refresh = useCallback(async () => {
    setScanning(true);
    setMsg('');
    try {
      const r = await listCameras();
      setDevices(r.devices);
      setCurrent(r.current);
      setPlatform(r.platform ?? 'unknown');
      setInContainer(!!r.in_container);
      if (!selected) setSelected(r.current || (r.devices[0]?.id ?? ''));
    } catch (e: any) {
      setMsg(`scan failed: ${e?.message ?? e}`);
    } finally {
      setScanning(false);
    }
  }, [selected]);

  // initial scan on mount
  useEffect(() => { refresh(); }, [refresh]);

  const apply = async (source: string) => {
    if (!source) { setMsg('select a device or enter a URL'); return; }
    setApplying(true);
    setMsg('');
    try {
      const r = await setCameraSource(source);
      if (r.ok) { setMsg(`switched to ${r.source}`); setCurrent(r.source); }
      else setMsg('switch rejected');
    } catch (e: any) {
      setMsg(`apply failed: ${e?.message ?? e}`);
    } finally {
      setApplying(false);
    }
  };

  if (!s) return <div>Loading stats…</div>;

  const cameraDown = !s.camera.connected;

  return (
    <div>
      <h2>Stats</h2>
      <div className="kpi">
        <div className="item">
          <div className="lbl">Camera</div>
          <div className="val">
            <span className={s.camera.connected ? 'tag-ok' : 'tag-bad'}>
              {s.camera.connected ? '● live' : '○ down'}
            </span>{' '}
            <small>{s.camera.fps.toFixed(1)} fps</small>
          </div>
        </div>
        <div className="item">
          <div className="lbl">Inference</div>
          <div className="val">{s.inference.latency_ms.toFixed(1)} ms <small>({s.inference.count})</small></div>
        </div>
        <div className="item">
          <div className="lbl">Active tracks</div>
          <div className="val">{s.tracker.active}</div>
        </div>
        <div className="item">
          <div className="lbl">DB rows</div>
          <div className="val">{s.storage.rows}</div>
        </div>
      </div>

      <div style={{ marginTop: 12 }}>
        <div className="lbl" style={{ marginBottom: 4 }}>
          CAMERA SOURCE {cameraDown && <span className="tag-bad" style={{ marginLeft: 6 }}>not connected</span>}
        </div>
        <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap', alignItems: 'center' }}>
          <select
            value={selected}
            onChange={(e) => setSelected(e.target.value)}
            style={{ minWidth: 220 }}
          >
            {devices.length === 0 && <option value="">(no devices found)</option>}
            {devices.map((d) => (
              <option key={d.id} value={d.id}>
                {d.label}{d.id === current ? ' — current' : ''}
              </option>
            ))}
          </select>
          <button onClick={refresh} disabled={scanning}>
            {scanning ? 'Scanning…' : 'Refresh'}
          </button>
          <button onClick={() => apply(selected)} disabled={applying || !selected}>
            {applying ? 'Applying…' : 'Apply'}
          </button>
        </div>

        <div style={{ display: 'flex', gap: 6, marginTop: 6 }}>
          <input
            type="text"
            placeholder="Custom URL (e.g. rtsp://… or /dev/video2)"
            value={customUrl}
            onChange={(e) => setCustomUrl(e.target.value)}
            style={{ flex: 1, minWidth: 220 }}
          />
          <button onClick={() => apply(customUrl.trim())} disabled={applying || !customUrl.trim()}>
            Apply URL
          </button>
        </div>

        <div style={{ marginTop: 4, fontSize: 12, opacity: 0.8 }}>
          current: <code>{current || '(unknown)'}</code>
          {msg && <span style={{ marginLeft: 8 }}>{msg}</span>}
        </div>
        {devices.length === 0 && (
          <div style={{ marginTop: 4, fontSize: 11, opacity: 0.7, lineHeight: 1.45 }}>
            No cameras detected. Platform: <code>{platform}</code>
            {inContainer && ' (running inside container)'}.
            {inContainer && platform === 'linux' && (
              <> Expose host cameras via <code>devices:</code> in <code>ops/docker-compose.yml</code>, or run core natively on the host.</>
            )}
            {inContainer && platform !== 'linux' && (
              <> Docker Desktop on macOS/Windows cannot pass host USB cameras into the Linux VM. Run core natively on the host: <code>make -C ops dev-core</code>.</>
            )}
            {!inContainer && platform === 'macos' && (
              <> Check System Settings → Privacy & Security → Camera, and grant the terminal/binary access.</>
            )}
          </div>
        )}
      </div>
    </div>
  );
}
