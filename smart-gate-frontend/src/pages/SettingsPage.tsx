import React, { useState, useEffect } from 'react';
import { useApp } from '../App';
import {
  IconSettings, IconRadio, IconClock, IconActivity, IconMinus, IconPlus, IconCheck, IconRefresh,
} from '../Icons';

interface Settings {
  frequency: number;
  bitRate: number;
  frequencyDeviation: number;
  rxBandwidth: number;
  outputPower: number;
  rssi: number;
}

// Реальная рабочая конфигурация радио (см. CC1101Manager::init): OOK650, 20 kbps, RX BW 135 кГц.
const DEFAULTS: Omit<Settings, 'rssi'> = {
  frequency: 433.92,
  bitRate: 20.0,
  frequencyDeviation: 5.2,
  rxBandwidth: 135.0,
  outputPower: 10,
};

const PRESETS = [
  { label: '433.92', value: 433.92, desc: 'Стандарт EU' },
  { label: '868.35', value: 868.35, desc: 'EU ISM' },
  { label: '315.00', value: 315.0, desc: 'US/Asia' },
  { label: '390.00', value: 390.0, desc: 'Chamberlain' },
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
      // Тайминги ворот сохраняются в прошивку (userdata NVS), а не только в браузер
      await apiCall('/api/gate/config', 'POST', {
        openDuration: gateTimings.openDuration,
        stayOpen: gateTimings.stayOpen,
        closeDuration: gateTimings.closeDuration,
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
        <div className="page-title"><IconSettings size={22} /> Настройки</div>
        <div className="section">
          <div className="empty"><div className="empty-title">Загрузка...</div></div>
        </div>
      </div>
    );
  }

  // Уровень RSSI для шкалы: −110…−30 dBm → 0…100 %
  const rssiPct = settings.rssi
    ? Math.max(0, Math.min(100, ((settings.rssi + 110) / 80) * 100))
    : 0;

  return (
    <div>
      <div className="page-title"><IconSettings size={22} /> Настройки</div>

      {/* Статус радиомодуля */}
      <div className="section">
        <div className="section-header">
          <IconActivity size={14} />
          Радиомодуль CC1101
          <span className="section-header-aux">
            <span className="badge badge--green">Активен</span>
          </span>
        </div>
        <div className="section-pad">
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 8, marginBottom: 8 }}>
            <span style={{ fontSize: 22, fontWeight: 700, fontFamily: 'var(--mono)', color: 'var(--accent)' }}>
              {settings.rssi ? `${settings.rssi} dBm` : '—'}
            </span>
            <span style={{ fontSize: 12, color: 'var(--text-muted)' }}>уровень эфира</span>
          </div>
          <div className="meter" aria-hidden="true">
            <div className="meter-fill" style={{ width: `${rssiPct}%` }} />
          </div>
        </div>
      </div>

      {/* Частота */}
      <div className="section">
        <div className="section-header">
          <IconRadio size={14} />
          Частота
        </div>
        <div className="section-pad">
          <div className="seg seg--2" style={{ marginBottom: 12 }}>
            {PRESETS.map(p => (
              <button
                key={p.value}
                className={`seg-btn ${settings.frequency === p.value ? 'seg-btn--active' : ''}`}
                onClick={() => set('frequency', p.value)}
              >
                {p.label} МГц
                <span className="seg-btn-desc">{p.desc}</span>
              </button>
            ))}
          </div>
          <SettingRow
            label="Частота"
            unit="МГц"
            value={settings.frequency}
            onChange={v => set('frequency', v)}
            min={300} max={928} step={0.01}
          />
        </div>
      </div>

      {/* Таймер ворот */}
      <div className="section">
        <div className="section-header">
          <IconClock size={14} />
          Цикл ворот
        </div>
        <div className="section-pad" style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
          <SettingRow label="Открытие" unit="сек" value={gateTimings.openDuration} onChange={v => { setGateTimings({ ...gateTimings, openDuration: Math.max(1, v) }); setChanged(true); }} min={1} max={60} step={1} />
          <SettingRow label="Открыто" unit="сек" value={gateTimings.stayOpen} onChange={v => { setGateTimings({ ...gateTimings, stayOpen: Math.max(1, v) }); setChanged(true); }} min={1} max={300} step={1} />
          <SettingRow label="Закрытие" unit="сек" value={gateTimings.closeDuration} onChange={v => { setGateTimings({ ...gateTimings, closeDuration: Math.max(1, v) }); setChanged(true); }} min={1} max={60} step={1} />
        </div>
      </div>

      {/* Модуляция */}
      <div className="section">
        <div className="section-header">
          <IconSettings size={14} />
          Модуляция
        </div>
        <div className="section-pad" style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
          <SettingRow label="Битрейт" unit="kbps" value={settings.bitRate} onChange={v => set('bitRate', v)} min={0.1} max={500} step={0.01} />
          <SettingRow label="Девиация" unit="кГц" value={settings.frequencyDeviation} onChange={v => set('frequencyDeviation', v)} min={0.1} max={300} step={0.1} />
          <SettingRow label="RX полоса" unit="кГц" value={settings.rxBandwidth} onChange={v => set('rxBandwidth', v)} min={0.1} max={800} step={0.1} />
          <SettingRow label="Мощность" unit="dBm" value={settings.outputPower} onChange={v => set('outputPower', v)} min={-30} max={10} step={1} />
        </div>
      </div>

      {/* Действия: при несохранённых изменениях панель прилипает к низу */}
      <div className={changed ? 'savebar' : 'actions-row'}>
        <button className="btn btn--ghost" style={{ flex: 1 }} onClick={reset} disabled={saving}>
          <IconRefresh size={15} />
          Сброс
        </button>
        <button className="btn btn--primary" style={{ flex: 2 }} onClick={save} disabled={saving || !changed}>
          {saving ? 'Сохранение...' : (<><IconCheck size={16} /> Сохранить</>)}
        </button>
      </div>
    </div>
  );
};

// --- Строка настройки: степпер [−] значение [+] с единицей измерения ---
const SettingRow: React.FC<{
  label: string;
  unit: string;
  value: number;
  onChange: (v: number) => void;
  min: number;
  max: number;
  step: number;
}> = ({ label, unit, value, onChange, min, max, step }) => {
  // Держим строковый черновик инпута, чтобы поле можно было временно очистить,
  // а пустой ввод не подменялся нулём (0 ломает радио при сохранении).
  const [draft, setDraft] = useState(String(value));

  // Синхронизируем черновик при внешних изменениях (пресеты, «Сброс», загрузка).
  useEffect(() => { setDraft(String(value)); }, [value]);

  const commit = (raw: string) => {
    setDraft(raw);
    const v = parseFloat(raw);
    if (!Number.isNaN(v)) onChange(v); // валидное число — прокидываем наверх; пустое/мусор — игнорируем
  };

  // Точность шага, чтобы 0.01-шаги не накапливали хвосты float
  const decimals = (String(step).split('.')[1] || '').length;

  const stepBy = (dir: 1 | -1) => {
    const cur = parseFloat(draft);
    const basis = Number.isNaN(cur) ? value : cur;
    const next = Math.min(max, Math.max(min, +(basis + dir * step).toFixed(decimals)));
    onChange(next);
  };

  return (
    <div className="setting-row">
      <span className="setting-label">{label}</span>
      <div className="stepper">
        <button className="stepper-btn" onClick={() => stepBy(-1)} aria-label={`${label}: уменьшить`} type="button">
          <IconMinus size={15} />
        </button>
        <input
          className="input"
          type="number"
          value={draft}
          onChange={e => commit(e.target.value)}
          onBlur={() => { if (draft.trim() === '' || Number.isNaN(parseFloat(draft))) setDraft(String(value)); }}
          min={min} max={max} step={step}
          aria-label={`${label}, ${unit}`}
        />
        <button className="stepper-btn" onClick={() => stepBy(1)} aria-label={`${label}: увеличить`} type="button">
          <IconPlus size={15} />
        </button>
        <span className="stepper-unit">{unit}</span>
      </div>
    </div>
  );
};

export default SettingsPage;
