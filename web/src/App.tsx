import { useEffect, useState } from 'react';
import { LivePanel } from './components/LivePanel';
import { StatsPanel } from './components/StatsPanel';
import { TracksPanel } from './components/TracksPanel';
import { PosePanel } from './components/PosePanel';
import { LiveTrack, openEventStream } from './api/oscar';

export function App() {
  const [tracks, setTracks] = useState<LiveTrack[]>([]);

  useEffect(() => {
    const es = openEventStream(setTracks);
    return () => es.close();
  }, []);

  return (
    <div className="layout">
      <div className="panel">
        <h1>OSCAR — Live</h1>
        <LivePanel tracks={tracks} />
      </div>
      <div className="panel" style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        <StatsPanel />
        <PosePanel />
        <TracksPanel live={tracks} />
      </div>
    </div>
  );
}
