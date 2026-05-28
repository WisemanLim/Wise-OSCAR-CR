import { useEffect, useState } from 'react';
import { getPose, setPose, Pose } from '../api/oscar';

export function PosePanel() {
  const [p, setP] = useState<Pose>({
    lat: 37.5665, lon: 126.9780, alt: 50,
    yaw: 0, pitch: 0, roll: 0, ts_ms: 0,
  });

  useEffect(() => {
    getPose().then((q) => { if (q && (q.lat || q.lon)) setP(q); }).catch(() => { /* ignore */ });
  }, []);

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    await setPose({ ...p, ts_ms: Date.now() });
  };

  const set = <K extends keyof Pose>(k: K, v: number) => setP({ ...p, [k]: v });

  const useGeo = () => {
    if (!navigator.geolocation) return alert('Geolocation unsupported');
    navigator.geolocation.getCurrentPosition(
      (pos) => setP({ ...p,
        lat: pos.coords.latitude,
        lon: pos.coords.longitude,
        alt: pos.coords.altitude ?? p.alt,
      }),
      (err) => alert('Geolocation failed: ' + err.message)
    );
  };

  return (
    <div>
      <h2>Camera Pose</h2>
      <form onSubmit={submit}>
        <div className="row">
          <label>Lat <input type="number" step="0.000001" value={p.lat}
            onChange={(e) => set('lat', parseFloat(e.target.value))} /></label>
          <label>Lon <input type="number" step="0.000001" value={p.lon}
            onChange={(e) => set('lon', parseFloat(e.target.value))} /></label>
          <label>Alt <input type="number" step="0.1" value={p.alt}
            onChange={(e) => set('alt', parseFloat(e.target.value))} /></label>
        </div>
        <div className="row">
          <label>Yaw <input type="number" step="0.5" value={p.yaw}
            onChange={(e) => set('yaw', parseFloat(e.target.value))} /></label>
          <label>Pitch <input type="number" step="0.5" value={p.pitch}
            onChange={(e) => set('pitch', parseFloat(e.target.value))} /></label>
          <label>Roll <input type="number" step="0.5" value={p.roll}
            onChange={(e) => set('roll', parseFloat(e.target.value))} /></label>
        </div>
        <div className="row">
          <button type="submit">Update Pose</button>
          <button type="button" onClick={useGeo}>Use Browser GNSS</button>
        </div>
      </form>
    </div>
  );
}
