import React, { useState, useEffect } from 'react';
import { useApp } from '../App';

interface Settings {
  frequency: number;
  bitRate: number;
  frequencyDeviation: number;
  rxBandwidth: number;
  outputPower: number;
  rssi: number;
}

const DEFAULTS: Omit<Settings, 'rssi'> = {
  frequency: 433.92,
  bitRate: 3.79,
  frequencyDeviation: 5.2,
  rxBandwidth: 58.0,
  outputPower: 10,
};

const PRESETS = [
  { label: '433.92 МГц', value: 433.92, desc: 'Стандарт EU' },
  { label: '868.35 МГц', value: 868.35, desc: 'EU ISM' },
  { label: '315.00 МГц', value: 315.0, desc: 'US/Asia' },
  { label: '390.00 МГц', value: 390.0, desc: 'Chamberlain' },
];

const SettingsPage: React.FC = () => {
  const { apiCall, addLog, gateTimings, setGateTimings } = useApp();
  const [settings, setSettings] = useState<Settings>({ ...DEFAULTS, rssi: 0 });
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [changed, setChanged] = useState(false);

  useEffect(() => {
    (async () => {
      try {
        const cfg = await apiCall('/api/cc1101/config');
        setSettings({
          frequency: cfg.frequency || DEFAULTS.frequency,
          bitRate: cfg.bitRate || DEFAULTS.bitRate,
          frequencyDeviation: cfg.frequencyDeviation || DEFAULTS.frequencyDeviation,
          rxBandwidth: cfg.rxBandwidth || DEFAULTS.rxBandwidth,
          outputPower: cfg.outputPower ?? DEFAULTS.outputPower,
          rssi: cfg.rssi || 0,
        });
      } catch {
        addLog('Ошибка загрузки настроек', 'error');
      } finally {
        setLoading(false);
      }
    })();
  }, [apiCall, addLog]);

  const set = (field: keyof Settings, value: number) => {
    setSettings(prev => ({ ...prev, [field]: value }));
    setChanged(true);
  };

  const save = async () => {
    setSaving(true);
    try {
      const result = await apiCall('/api/cc1101/settings', 'POST', {
        frequency: settings.frequency,
        bitRate: settings.bitRate,
        frequencyDeviation: settings.frequencyDeviation,
        rxBandwidth: settings.rxBandwidth,
        outputPower: settings.outputPower,
      });
      if (result.success) {
        addLog('Настройки сохранены', 'success');
        setChanged(false);
      } else {
        addLog(`Ошибка: ${result.error}`, 'error');
      }
    } catch {
      addLog('Ошибка сохранения', 'error');
    } finally {
      setSaving(false);
    }
  };

  const reset = () => {
    setSettings(prev => ({ ...DEFAULTS, rssi: prev.rssi }));
    setChanged(true);
  };

  if (loading) {
    return (
      <div>
        <div className="page-title">Настройки</div>
        <div className="empty"><div className="empty-title">Загрузка...</div></div>
      </div>
    );
  }

  return (
    <div>
      <div className="page-title">Настройки CC1101</div>

      {/* Status */}
      <div className="section" style={{ padding: 14, marginBottom: 12 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <div>
            <div style={{ fontSize: 12, color: 'var(--text-muted)', fontWeight: 600, textTransform: 'uppercase', letterSpacing: 0.5 }}>
              RSSI
            </div>
            <div style={{ fontSize: 20, fontWeight: 700, fontFamily: 'var(--mono)', color: 'var(--accent)' }}>
              {settings.rssi} dBm
            </div>
          </div>
          <span className="badge badge--green">Активен</span>
        </div>
      </div>

      {/* Frequency presets */}
      <div className="section">
        <div className="section-header">Частота</div>
        <div style={{ padding: 14 }}>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8, marginBottom: 12 }}>
            {PRESETS.map(p => (
              <button
                key={p.value}
                className={`btn ${settings.frequency === p.value ? 'btn--primary' : 'btn--ghost'}`}
                style={{ padding: '8px 0', fontSize: 12 }}
                onClick={() => set('frequency', p.value)}
              >
                {p.label}
                <div style={{ fontSize: 10, opacity: 0.7, marginTop: 2 }}>{p.desc}</div>
              </button>
            ))}
          </div>
          <label style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{ fontSize: 13, color: 'var(--text-dim)', flexShrink: 0 }}>Частота (МГц):</span>
            <input
              className="input"
              type="number"
              value={settings.frequency}
              onChange={e => set('frequency', parseFloat(e.target.value) || 0)}
              min={300} max={928} step={0.01}
              style={{ fontFamily: 'var(--mono)' }}
            />
          </label>
        </div>
      </div>

      {/* Gate timings */}
      <div className="section">
        <div className="section-header">Таймер ворот</div>
        <div style={{ padding: 14, display: 'flex', flexDirection: 'column', gap: 12 }}>
          <SettingRow label="Открытие (сек)" value={gateTimings.openDuration} onChange={v => setGateTimings({ ...gateTimings, openDuration: Math.max(1, v) })} min={1} max={60} step={1} />
          <SettingRow label="Открыто (сек)" value={gateTimings.stayOpen} onChange={v => setGateTimings({ ...gateTimings, stayOpen: Math.max(1, v) })} min={1} max={300} step={1} />
          <SettingRow label="Закрытие (сек)" value={gateTimings.closeDuration} onChange={v => setGateTimings({ ...gateTimings, closeDuration: Math.max(1, v) })} min={1} max={60} step={1} />
        </div>
      </div>

      {/* Modulation */}
      <div className="section">
        <div className="section-header">Модуляция</div>
        <div style={{ padding: 14, display: 'flex', flexDirection: 'column', gap: 12 }}>
          <SettingRow label="Битрейт (kbps)" value={settings.bitRate} onChange={v => set('bitRate', v)} min={0.1} max={500} step={0.01} />
          <SettingRow label="Девиация (кГц)" value={settings.frequencyDeviation} onChange={v => set('frequencyDeviation', v)} min={0.1} max={300} step={0.1} />
          <SettingRow label="RX полоса (кГц)" value={settings.rxBandwidth} onChange={v => set('rxBandwidth', v)} min={0.1} max={800} step={0.1} />
          <SettingRow label="Мощность (dBm)" value={settings.outputPower} onChange={v => set('outputPower', v)} min={-30} max={10} step={1} />
        </div>
      </div>

      {/* Actions */}
      <div style={{ display: 'flex', gap: 8, marginTop: 4 }}>
        <button className="btn btn--ghost" style={{ flex: 1 }} onClick={reset} disabled={saving}>
          Сброс
        </button>
        <button className="btn btn--primary" style={{ flex: 2 }} onClick={save} disabled={saving || !changed}>
          {saving ? 'Сохранение...' : 'Сохранить'}
        </button>
      </div>
    </div>
  );
};

// --- Setting row ---
const SettingRow: React.FC<{
  label: string;
  value: number;
  onChange: (v: number) => void;
  min: number;
  max: number;
  step: number;
}> = ({ label, value, onChange, min, max, step }) => (
  <label style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
    <span style={{ fontSize: 13, color: 'var(--text-dim)', flexShrink: 0, minWidth: 120 }}>{label}</span>
    <input
      className="input"
      type="number"
      value={value}
      onChange={e => onChange(parseFloat(e.target.value) || 0)}
      min={min} max={max} step={step}
      style={{ fontFamily: 'var(--mono)' }}
    />
  </label>
);

export default SettingsPage;
