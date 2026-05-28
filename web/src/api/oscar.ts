const BASE = (import.meta as any).env?.VITE_OSCAR_API || '';

export interface Stats {
  camera: { connected: boolean; fps: number; seq: number };
  inference: { count: number; latency_ms: number };
  tracker: { active: number };
  storage: { rows: number };
  pose_known: boolean;
}

export interface Pose {
  lat: number; lon: number; alt: number;
  yaw: number; pitch: number; roll: number;
  ts_ms: number;
}

export interface LiveTrack {
  id: number;
  category: string;
  state: string;
  class_name: string;
  score: number;
  bbox: [number, number, number, number];
  rel_pos: [number, number, number];
  abs_pos: [number, number, number];
  velocity_px: [number, number];
  ts_ms: number;
}

export interface CameraDevice {
  id: string;
  label: string;
  kind: 'index' | 'device' | 'url' | string;
  available: boolean;
}

export interface CameraDevicesResp {
  devices: CameraDevice[];
  current: string;
  platform?: 'macos' | 'linux' | 'windows' | 'unknown' | string;
  in_container?: boolean;
}

export async function getStats(): Promise<Stats> {
  return (await fetch(`${BASE}/api/stats`)).json();
}

export async function listCameras(): Promise<CameraDevicesResp> {
  return (await fetch(`${BASE}/api/camera/devices`)).json();
}

export async function setCameraSource(source: string): Promise<{ ok: boolean; source: string }> {
  return (await fetch(`${BASE}/api/camera/source`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source }),
  })).json();
}

export async function getPose(): Promise<Pose> {
  return (await fetch(`${BASE}/api/pose`)).json();
}

export async function setPose(p: Partial<Pose>) {
  return fetch(`${BASE}/api/pose`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(p),
  });
}

export async function getRecent(limit = 50) {
  return (await fetch(`${BASE}/api/tracks?limit=${limit}`)).json();
}

export function openEventStream(onMsg: (data: LiveTrack[]) => void): EventSource {
  const es = new EventSource(`${BASE}/api/events`);
  es.onmessage = (e) => {
    try { onMsg(JSON.parse(e.data)); } catch { /* noop */ }
  };
  return es;
}

export const frameUrl = (cacheBust = 0) => `${BASE}/api/frame.jpg?t=${cacheBust}`;
