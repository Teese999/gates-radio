import React, { useState, useEffect, useCallback, useRef } from 'react';
import { useApp } from '../App';

interface Key {
  code: number;
  name: string;
  enabled: boolean;
  bitLength: number;
  protocol: string;
  te: number;
  frequency: number;
  modulation: string;
  rssi: number;
  timestamp: number;
}

interface SignalEvent {
  rssi: number;
  protocol: string;
  bitLength: number;
  timestamp: number;
}

const FREQ_PRESETS = [
  { label: '315.00', value: 315.0 },
  { label: '390.00', value: 390.0 },
  { label: '433.92', value: 433.92 },
  { label: '434.42', value: 434.42 },
  { label: '868.35', value: 868.35 },
  { label: '915.00', value: 915.0 },
];

// --- Real RSSI spectrogram (scrolling waterfall like SDR) ---
const RSSI_HISTORY_LEN = 160;

const SignalSpectrogram: React.FC<{
  rssiHistory: number[];
  signals: SignalEvent[];
  frequency: number;
}> = ({ rssiHistory, signals, frequency }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const animRef = useRef(0);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const W = canvas.width;
    const H = canvas.height;
    let running = true;

    const draw = () => {
      if (!running) return;

      // Background
      ctx.fillStyle = '#0a0c10';
      ctx.fillRect(0, 0, W, H);

      // Grid
      ctx.strokeStyle = '#151820';
      ctx.lineWidth = 1;
      for (let y = 0; y < H; y += 20) {
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
      }

      const len = rssiHistory.length;
      if (len < 2) {
        animRef.current = requestAnimationFrame(draw);
        return;
      }

      // RSSI range: -110 (bottom) to -30 (top)
      const rssiToY = (rssi: number) => {
        const clamped = Math.max(-110, Math.min(-30, rssi));
        return H - ((clamped + 110) / 80) * H;
      };

      // Noise floor reference line at -90 dBm
      const noiseY = rssiToY(-90);
      ctx.strokeStyle = '#2e334540';
      ctx.setLineDash([4, 4]);
      ctx.beginPath(); ctx.moveTo(0, noiseY); ctx.lineTo(W, noiseY); ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = '#2e3345';
      ctx.font = '8px monospace';
      ctx.fillText('-90', 2, noiseY - 2);

      // Filled area under RSSI curve (waterfall effect)
      const step = W / (RSSI_HISTORY_LEN - 1);
      const startIdx = Math.max(0, len - RSSI_HISTORY_LEN);

      // Gradient fill
      ctx.beginPath();
      ctx.moveTo(0, H);
      for (let i = 0; i < Math.min(len, RSSI_HISTORY_LEN); i++) {
        const x = i * step;
        const y = rssiToY(rssiHistory[startIdx + i]);
        ctx.lineTo(x, y);
      }
      ctx.lineTo((Math.min(len, RSSI_HISTORY_LEN) - 1) * step, H);
      ctx.closePath();

      const grad = ctx.createLinearGradient(0, 0, 0, H);
      grad.addColorStop(0, 'rgba(79, 140, 255, 0.4)');
      grad.addColorStop(0.5, 'rgba(79, 140, 255, 0.1)');
      grad.addColorStop(1, 'rgba(79, 140, 255, 0.02)');
      ctx.fillStyle = grad;
      ctx.fill();

      // RSSI line
      ctx.strokeStyle = '#4f8cff';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      for (let i = 0; i < Math.min(len, RSSI_HISTORY_LEN); i++) {
        const x = i * step;
        const y = rssiToY(rssiHistory[startIdx + i]);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();

      // Signal burst markers
      const now = Date.now();
      for (const sig of signals) {
        const age = (now - sig.timestamp) / 1000;
        if (age > 6) continue;
        const fadeout = Math.max(0, 1 - age / 6);

        // Position on timeline
        const xPos = W - (age / (RSSI_HISTORY_LEN * 0.2)) * W;
        if (xPos < 0 || xPos > W) continue;

        const sigY = rssiToY(sig.rssi);

        // Spike marker
        ctx.fillStyle = `rgba(52, 211, 153, ${fadeout * 0.8})`;
        ctx.beginPath();
        ctx.arc(xPos, sigY, 3, 0, Math.PI * 2);
        ctx.fill();

        // Label
        if (fadeout > 0.4) {
          ctx.fillStyle = `rgba(52, 211, 153, ${fadeout})`;
          ctx.font = 'bold 9px monospace';
          ctx.fillText(sig.protocol, xPos + 5, sigY - 4);
          ctx.font = '8px monospace';
          ctx.fillText(`${sig.rssi}dBm`, xPos + 5, sigY + 8);
        }
      }

      // Current RSSI value (top right)
      if (len > 0) {
        const current = rssiHistory[len - 1];
        const color = current > -60 ? '#34d399' : current > -80 ? '#fbbf24' : '#f87171';
        ctx.fillStyle = color;
        ctx.font = 'bold 14px monospace';
        ctx.textAlign = 'right';
        ctx.fillText(`${current} dBm`, W - 4, 16);
        ctx.textAlign = 'left';
      }

      // Frequency label (top left)
      ctx.fillStyle = '#5c6070';
      ctx.font = '10px monospace';
      ctx.fillText(`${frequency} MHz`, 4, 12);

      animRef.current = requestAnimationFrame(draw);
    };

    draw();
    return () => { running = false; cancelAnimationFrame(animRef.current); };
  }, [rssiHistory, signals, frequency]);

  return (
    <canvas
      ref={canvasRef}
      width={320}
      height={120}
      style={{
        width: '100%',
        height: 120,
        borderRadius: 'var(--radius)',
        border: '1px solid var(--border)',
        display: 'block',
      }}
    />
  );
};

// --- Main component ---
const KeyPage: React.FC = () => {
  const { apiCall, addLog, subscribe } = useApp();
  const [keys, setKeys] = useState<Key[]>([]);
  const [loading, setLoading] = useState(true);
  const [learning, setLearning] = useState(false);
  const [deleteTarget, setDeleteTarget] = useState<number | null>(null);
  const [editTarget, setEditTarget] = useState<number | null>(null);
  const [editName, setEditName] = useState('');
  const [signals, setSignals] = useState<SignalEvent[]>([]);
  const [rssiHistory, setRssiHistory] = useState<number[]>([]);
  const [frequency, setFrequency] = useState(433.92);
  const [freqInput, setFreqInput] = useState('');
  const [changingFreq, setChangingFreq] = useState(false);

  // Load keys & frequency
  const loadKeys = useCallback(async () => {
    try {
      const data = await apiCall('/api/keys');
      if (Array.isArray(data)) setKeys(data);
    } catch {
      addLog('Ошибка загрузки ключей', 'error');
    } finally {
      setLoading(false);
    }
  }, [apiCall, addLog]);

  const loadFrequency = useCallback(async () => {
    try {
      const data = await apiCall('/api/frequency');
      if (data.frequency) {
        setFrequency(data.frequency);
        setFreqInput(String(data.frequency));
      }
    } catch {}
  }, [apiCall]);

  const checkStatus = useCallback(async () => {
    try {
      const data = await apiCall('/api/keys/status');
      setLearning(data.learningMode === true);
    } catch {}
  }, [apiCall]);

  useEffect(() => {
    loadKeys();
    checkStatus();
    loadFrequency();
  }, [loadKeys, checkStatus, loadFrequency]);

  // WS events
  useEffect(() => {
    const unsubs = [
      subscribe('key_added', () => {
        setLearning(false);
        loadKeys();
      }),
      subscribe('key_received', (data: any) => {
        setSignals(prev => [...prev, {
          rssi: data.rssi || -70,
          protocol: data.protocol || 'RAW',
          bitLength: data.bitLength || 0,
          timestamp: Date.now(),
        }].slice(-20));
      }),
      subscribe('rssi', (data: any) => {
        // Real-time RSSI from CC1101 (streamed every 200ms in learning mode)
        if (typeof data.rssi === 'number') {
          setRssiHistory(prev => [...prev.slice(-(RSSI_HISTORY_LEN - 1)), data.rssi]);
        }
      }),
    ];
    return () => unsubs.forEach(fn => fn());
  }, [subscribe, loadKeys]);

  // Cleanup old signals
  useEffect(() => {
    const interval = setInterval(() => {
      setSignals(prev => prev.filter(s => Date.now() - s.timestamp < 10000));
    }, 2000);
    return () => clearInterval(interval);
  }, []);

  const changeFrequency = async (freq: number) => {
    setChangingFreq(true);
    try {
      const result = await apiCall('/api/frequency/set', 'POST', { frequency: freq });
      if (result.success) {
        setFrequency(freq);
        setFreqInput(String(freq));
        addLog(`Частота: ${freq} МГц`, 'success');
      } else {
        addLog(`Ошибка: ${result.error}`, 'error');
      }
    } catch {
      addLog('Ошибка смены частоты', 'error');
    } finally {
      setChangingFreq(false);
    }
  };

  const startLearning = async () => {
    try {
      await apiCall('/api/keys/learn', 'POST');
      setLearning(true);
      setSignals([]);
      setRssiHistory([]);
      addLog('Режим обучения активирован', 'info');
    } catch {
      addLog('Ошибка активации обучения', 'error');
    }
  };

  const stopLearning = async () => {
    try {
      await apiCall('/api/keys/stop', 'POST');
      setLearning(false);
      addLog('Режим обучения остановлен', 'warning');
    } catch {
      addLog('Ошибка остановки обучения', 'error');
    }
  };

  const toggleKey = async (code: number, enabled: boolean) => {
    try {
      await apiCall('/api/keys/update', 'PUT', { code, enabled });
      setKeys(prev => prev.map(k => k.code === code ? { ...k, enabled } : k));
    } catch {
      addLog('Ошибка обновления ключа', 'error');
    }
  };

  const deleteKey = async (code: number) => {
    try {
      await apiCall('/api/keys/delete', 'POST', { code });
      setKeys(prev => prev.filter(k => k.code !== code));
      addLog('Ключ удалён', 'success');
    } catch {
      addLog('Ошибка удаления ключа', 'error');
    } finally {
      setDeleteTarget(null);
    }
  };

  const renameKey = async () => {
    if (!editTarget || !editName.trim()) return;
    try {
      await apiCall('/api/keys/update', 'PUT', { code: editTarget, name: editName.trim() });
      setKeys(prev => prev.map(k => k.code === editTarget ? { ...k, name: editName.trim() } : k));
      addLog('Имя ключа обновлено', 'success');
    } catch {
      addLog('Ошибка переименования', 'error');
    } finally {
      setEditTarget(null);
      setEditName('');
    }
  };

  return (
    <div>
      <div className="page-title">Ключи</div>

      {/* Frequency selector — always visible */}
      <div className="section" style={{ marginBottom: 12 }}>
        <div className="section-header">
          Частота: {frequency} МГц
        </div>
        <div style={{ padding: 10 }}>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginBottom: 8 }}>
            {FREQ_PRESETS.map(p => (
              <button
                key={p.value}
                className={`btn ${frequency === p.value ? 'btn--primary' : 'btn--ghost'}`}
                style={{ padding: '5px 10px', fontSize: 11, flex: '1 0 28%' }}
                onClick={() => changeFrequency(p.value)}
                disabled={changingFreq}
              >
                {p.label}
              </button>
            ))}
          </div>
          <div style={{ display: 'flex', gap: 6 }}>
            <input
              className="input"
              type="number"
              value={freqInput}
              onChange={e => setFreqInput(e.target.value)}
              onKeyDown={e => {
                if (e.key === 'Enter') {
                  const v = parseFloat(freqInput);
                  if (v >= 300 && v <= 928) changeFrequency(v);
                }
              }}
              placeholder="МГц"
              min={300} max={928} step={0.01}
              style={{ fontFamily: 'var(--mono)', fontSize: 13, flex: 1 }}
              disabled={changingFreq}
            />
            <button
              className="btn btn--primary"
              style={{ fontSize: 12, padding: '8px 14px' }}
              onClick={() => {
                const v = parseFloat(freqInput);
                if (v >= 300 && v <= 928) changeFrequency(v);
              }}
              disabled={changingFreq || !freqInput}
            >
              {changingFreq ? '...' : 'Set'}
            </button>
          </div>
        </div>
      </div>

      {/* Learning mode */}
      {!learning ? (
        <button className="btn btn--primary btn--full" onClick={startLearning} disabled={loading} style={{ marginBottom: 12 }}>
          Обучить новый ключ
        </button>
      ) : (
        <>
          {/* Real-time RSSI spectrogram */}
          <div style={{ marginBottom: 10 }}>
            <SignalSpectrogram rssiHistory={rssiHistory} signals={signals} frequency={frequency} />
          </div>

          <div className="learning-bar">
            <div className="learning-dot" />
            <span className="learning-text">Ожидание сигнала на {frequency} МГц...</span>
          </div>

          {/* Last signal info */}
          {signals.length > 0 && (
            <div className="section" style={{ marginBottom: 10 }}>
              <div style={{ padding: '8px 12px', fontSize: 12, fontFamily: 'var(--mono)', color: 'var(--text-dim)' }}>
                {signals.slice(-3).reverse().map((s, i) => (
                  <div key={i} style={{ marginBottom: 2 }}>
                    <span className="badge badge--green" style={{ marginRight: 6 }}>{s.protocol}</span>
                    {s.bitLength} бит · {s.rssi} dBm
                  </div>
                ))}
              </div>
            </div>
          )}

          <button className="btn btn--danger btn--full" onClick={stopLearning} style={{ marginBottom: 12 }}>
            Остановить
          </button>
        </>
      )}

      {/* Keys list */}
      {loading ? (
        <div className="empty"><div className="empty-title">Загрузка...</div></div>
      ) : keys.length === 0 ? (
        <div className="section">
          <div className="empty">
            <div className="empty-title">Нет сохранённых ключей</div>
            <div className="empty-sub">Нажмите «Обучить» и нажмите кнопку на пульте</div>
          </div>
        </div>
      ) : (
        <div className="section">
          <div className="section-header">Сохранённые ключи ({keys.length})</div>
          {keys.map(key => (
            <div key={key.code} className="list-item" style={{ opacity: key.enabled ? 1 : 0.45 }}>
              <div className="list-item-body">
                <div
                  className="list-item-title"
                  onClick={() => { setEditTarget(key.code); setEditName(key.name); }}
                  style={{ cursor: 'pointer' }}
                >
                  {key.name}
                </div>
                <div className="list-item-sub">
                  <span className="badge badge--accent">{key.protocol}</span>
                  {' '}
                  <span style={{ fontSize: 11, color: 'var(--text-muted)' }}>
                    {key.bitLength} бит · {key.frequency} МГц · RSSI {key.rssi}
                  </span>
                </div>
              </div>
              <label className="toggle">
                <input type="checkbox" checked={key.enabled} onChange={() => toggleKey(key.code, !key.enabled)} />
                <span className="toggle-track" />
              </label>
              <button
                className="btn btn--ghost"
                style={{ padding: '6px 10px', fontSize: 12 }}
                onClick={() => setDeleteTarget(key.code)}
              >
                Удалить
              </button>
            </div>
          ))}
        </div>
      )}

      {/* Delete modal */}
      {deleteTarget !== null && (
        <div className="modal-overlay" onClick={() => setDeleteTarget(null)}>
          <div className="modal" onClick={e => e.stopPropagation()}>
            <div className="modal-title">Удалить ключ?</div>
            <div className="modal-text">
              {keys.find(k => k.code === deleteTarget)?.name || 'Ключ'} будет удалён безвозвратно.
            </div>
            <div className="modal-actions">
              <button className="btn btn--ghost" onClick={() => setDeleteTarget(null)}>Отмена</button>
              <button className="btn btn--danger" onClick={() => deleteKey(deleteTarget)}>Удалить</button>
            </div>
          </div>
        </div>
      )}

      {/* Rename modal */}
      {editTarget !== null && (
        <div className="modal-overlay" onClick={() => setEditTarget(null)}>
          <div className="modal" onClick={e => e.stopPropagation()}>
            <div className="modal-title">Переименовать</div>
            <input
              className="input"
              value={editName}
              onChange={e => setEditName(e.target.value)}
              onKeyDown={e => e.key === 'Enter' && renameKey()}
              autoFocus
              style={{ marginBottom: 16 }}
            />
            <div className="modal-actions">
              <button className="btn btn--ghost" onClick={() => setEditTarget(null)}>Отмена</button>
              <button className="btn btn--primary" onClick={renameKey} disabled={!editName.trim()}>
                Сохранить
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default KeyPage;
