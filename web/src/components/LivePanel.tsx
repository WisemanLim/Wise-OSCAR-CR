import { useEffect, useRef, useState } from 'react';
import { LiveTrack, frameUrl } from '../api/oscar';

export function LivePanel({ tracks }: { tracks: LiveTrack[] }) {
  const [tick, setTick] = useState(0);
  const imgRef = useRef<HTMLImageElement>(null);
  const [dim, setDim] = useState({ w: 1280, h: 720 });

  useEffect(() => {
    const t = setInterval(() => setTick((x) => x + 1), 100);   // 10 fps refresh
    return () => clearInterval(t);
  }, []);

  return (
    <div className="frame">
      <img
        ref={imgRef}
        src={frameUrl(tick)}
        onLoad={(e) => {
          const el = e.currentTarget;
          setDim({ w: el.naturalWidth || 1, h: el.naturalHeight || 1 });
        }}
        onError={() => { /* keep last frame */ }}
        alt="live"
      />
      <svg className="overlay" viewBox={`0 0 ${dim.w} ${dim.h}`} preserveAspectRatio="none">
        {tracks.map((t) => {
          const [x, y, w, h] = t.bbox;
          const color = t.state === 'Moving' ? '#6df0a1' : '#f0e16d';
          return (
            <g key={t.id}>
              <rect x={x} y={y} width={w} height={h} fill="none" stroke={color} strokeWidth={3} />
              <text x={x + 4} y={y - 6} fill={color} fontSize={18}>
                #{t.id} {t.class_name} {(t.score * 100).toFixed(0)}%
              </text>
            </g>
          );
        })}
      </svg>
      <div className="legend">
        Frame refresh @ 10 fps · Boxes from /api/events (SSE) · Yellow = Stationary, Green = Moving
      </div>
    </div>
  );
}
