import { LiveTrack } from '../api/oscar';

export function TracksPanel({ live }: { live: LiveTrack[] }) {
  return (
    <div>
      <h2>Tracked Objects ({live.length})</h2>
      <table className="table">
        <thead>
          <tr>
            <th>ID</th>
            <th>Class</th>
            <th>State</th>
            <th>BBox (x,y,w,h)</th>
            <th>Abs (x,y,z)</th>
          </tr>
        </thead>
        <tbody>
          {live.map((t) => (
            <tr key={t.id}>
              <td>#{t.id}</td>
              <td>{t.class_name || t.category}</td>
              <td>
                <span className={`pill ${t.state.toLowerCase()}`}>{t.state}</span>
              </td>
              <td>{t.bbox.map((n) => Math.round(n)).join(', ')}</td>
              <td>{t.abs_pos.map((n) => n.toFixed(2)).join(', ')}</td>
            </tr>
          ))}
        </tbody>
      </table>
      {live.length === 0 && <div className="legend">No active tracks (waiting on SSE…).</div>}
    </div>
  );
}
