import React, { useState, useEffect } from 'react';
import './SettingsPage.css';

interface CC1101Settings {
  frequency: number;
  bitRate: number;
  frequencyDeviation: number;
  rxBandwidth: number;
  outputPower: number;
  rssi: number;
}

interface SettingsPageProps {
  onBack: () => void;
  apiCall: (endpoint: string, method?: string, data?: any) => Promise<any>;
  addLog: (message: string, type?: 'info' | 'error' | 'success' | 'warning') => void;
}

function SettingsPage({ onBack, apiCall, addLog }: SettingsPageProps) {
  const [settings, setSettings] = useState<CC1101Settings>({
    frequency: 433.92,
    bitRate: 3.79,
    frequencyDeviation: 5.2,
    rxBandwidth: 58.0,
    outputPower: 10,
    rssi: 0
  });
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [changed, setChanged] = useState(false);

  // Загрузка текущих настроек
  useEffect(() => {
    loadSettings();
  }, []);

  const loadSettings = async () => {
    setLoading(true);
    try {
      const config = await apiCall('/api/cc1101/config');
      setSettings({
        frequency: config.frequency || 433.92,
        bitRate: config.bitRate || 3.79,
        frequencyDeviation: config.frequencyDeviation || 5.2,
        rxBandwidth: config.rxBandwidth || 58.0,
        outputPower: config.outputPower || 10,
        rssi: config.rssi || 0
      });
      setChanged(false);
    } catch (error) {
      addLog('❌ Ошибка загрузки настроек', 'error');
    } finally {
      setLoading(false);
    }
  };

  const handleChange = (field: keyof CC1101Settings, value: number) => {
    setSettings(prev => ({ ...prev, [field]: value }));
    setChanged(true);
  };

  const handleSave = async () => {
    setSaving(true);
    addLog('💾 Сохранение настроек...', 'info');
    
    try {
      const result = await apiCall('/api/cc1101/settings', 'POST', {
        frequency: settings.frequency,
        bitRate: settings.bitRate,
        frequencyDeviation: settings.frequencyDeviation,
        rxBandwidth: settings.rxBandwidth,
        outputPower: settings.outputPower
      });
      
      if (result.success) {
        addLog('✅ Настройки успешно сохранены', 'success');
        setSettings(prev => ({
          ...prev,
          frequency: result.frequency,
          bitRate: result.bitRate,
          frequencyDeviation: result.frequencyDeviation,
          rxBandwidth: result.rxBandwidth,
          outputPower: result.outputPower
        }));
        setChanged(false);
      } else {
        addLog(`❌ Ошибка сохранения: ${result.error}`, 'error');
      }
    } catch (error) {
      addLog('❌ Ошибка сохранения настроек', 'error');
    } finally {
      setSaving(false);
    }
  };

  const handleReset = () => {
    setSettings({
      frequency: 433.92,
      bitRate: 3.79,
      frequencyDeviation: 5.2,
      rxBandwidth: 58.0,
      outputPower: 10,
      rssi: settings.rssi
    });
    setChanged(true);
  };

  if (loading) {
    return (
      <div className="page-container">
        <div className="loading-state">
          <p>⏳ Загрузка настроек...</p>
        </div>
      </div>
    );
  }

  return (
    <div className="page-container">
      <div className="page-header">
        <button className="btn-back" onClick={onBack}>← Назад</button>
        <h2>⚙️ Настройки CC1101</h2>
        <div className="header-actions">
          <button 
            className="btn btn-secondary" 
            onClick={loadSettings}
            disabled={saving}
          >
            🔄 Обновить
          </button>
        </div>
      </div>

      <div className="settings-content">
        {/* Информация о текущем состоянии */}
        <div className="status-card">
          <h3>📊 Текущее состояние</h3>
          <div className="status-grid">
            <div className="status-item">
              <span className="status-label">RSSI:</span>
              <span className="status-value">{settings.rssi} dBm</span>
            </div>
            <div className="status-item">
              <span className="status-label">Статус:</span>
              <span className="status-value status-active">Активен</span>
            </div>
          </div>
        </div>

        {/* Настройки частоты */}
        <div className="settings-section">
          <h3>📡 Частота</h3>
          <div className="setting-item">
            <label>
              <span className="setting-label">Частота (МГц)</span>
              <span className="setting-description">Диапазон: 300 - 928 МГц</span>
            </label>
            <input
              type="number"
              value={settings.frequency}
              onChange={(e) => handleChange('frequency', parseFloat(e.target.value) || 0)}
              min="300"
              max="928"
              step="0.01"
              className="setting-input"
            />
          </div>
        </div>

        {/* Настройки модуляции */}
        <div className="settings-section">
          <h3>🔧 Параметры модуляции</h3>
          
          <div className="setting-item">
            <label>
              <span className="setting-label">Битрейт (kbps)</span>
              <span className="setting-description">Диапазон: 0.1 - 500 kbps</span>
            </label>
            <input
              type="number"
              value={settings.bitRate}
              onChange={(e) => handleChange('bitRate', parseFloat(e.target.value) || 0)}
              min="0.1"
              max="500"
              step="0.01"
              className="setting-input"
            />
          </div>

          <div className="setting-item">
            <label>
              <span className="setting-label">Девиация частоты (кГц)</span>
              <span className="setting-description">Диапазон: 0.1 - 300 кГц</span>
            </label>
            <input
              type="number"
              value={settings.frequencyDeviation}
              onChange={(e) => handleChange('frequencyDeviation', parseFloat(e.target.value) || 0)}
              min="0.1"
              max="300"
              step="0.1"
              className="setting-input"
            />
          </div>

          <div className="setting-item">
            <label>
              <span className="setting-label">Ширина полосы приемника (кГц)</span>
              <span className="setting-description">Диапазон: 0.1 - 800 кГц</span>
            </label>
            <input
              type="number"
              value={settings.rxBandwidth}
              onChange={(e) => handleChange('rxBandwidth', parseFloat(e.target.value) || 0)}
              min="0.1"
              max="800"
              step="0.1"
              className="setting-input"
            />
          </div>
        </div>

        {/* Настройки мощности */}
        <div className="settings-section">
          <h3>⚡ Выходная мощность</h3>
          <div className="setting-item">
            <label>
              <span className="setting-label">Мощность (dBm)</span>
              <span className="setting-description">Диапазон: -30 до 10 dBm</span>
            </label>
            <input
              type="number"
              value={settings.outputPower}
              onChange={(e) => handleChange('outputPower', parseInt(e.target.value) || 0)}
              min="-30"
              max="10"
              step="1"
              className="setting-input"
            />
            <div className="power-info">
              <small>Рекомендуемое значение: 10 dBm (максимальная мощность)</small>
            </div>
          </div>
        </div>

        {/* Кнопки действий */}
        <div className="settings-actions">
          <button 
            className="btn btn-danger" 
            onClick={handleReset}
            disabled={saving}
          >
            🔄 Сбросить по умолчанию
          </button>
          <button 
            className="btn btn-success" 
            onClick={handleSave}
            disabled={saving || !changed}
          >
            {saving ? '⏳ Сохранение...' : '💾 Сохранить настройки'}
          </button>
        </div>

        {/* Предупреждение */}
        <div className="settings-warning">
          <p>⚠️ <strong>Внимание:</strong> Изменение настроек может повлиять на качество приема сигналов. Рекомендуется использовать значения по умолчанию, если не уверены в их изменении.</p>
        </div>
      </div>
    </div>
  );
}

export default SettingsPage;

