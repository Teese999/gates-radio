import React, { useState, useEffect } from 'react';
import { useApp } from '../App';
import {
  IconWifi, IconWifiLevel, IconRefresh, IconLock, IconUnlock, IconEye, IconEyeOff, IconChevronRight,
} from '../Icons';

interface WiFiNetwork {
  ssid: string;
  rssi: number;
  encryption: number;
}

const WiFiPage: React.FC = () => {
  const { apiCall, addLog, wifiInfo, wifiStatus } = useApp();
  const [networks, setNetworks] = useState<WiFiNetwork[]>([]);
  const [scanning, setScanning] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [selected, setSelected] = useState<WiFiNetwork | null>(null);
  const [password, setPassword] = useState('');
  const [showPass, setShowPass] = useState(false);

  const scan = async () => {
    setScanning(true);
    try {
      const result = await apiCall('/api/wifi/scan');
      if (Array.isArray(result)) {
        setNetworks(result.sort((a, b) => b.rssi - a.rssi));
        addLog(`Найдено сетей: ${result.length}`, 'success');
      }
    } catch {
      addLog('Ошибка сканирования', 'error');
    } finally {
      setScanning(false);
    }
  };

  useEffect(() => { scan(); }, []);

  // Единая точка подключения: блокирует параллельные запросы (иначе они срывают друг друга)
  // и одинаково отражает результат для открытых и защищённых сетей.
  const connectTo = async (ssid: string, pass: string) => {
    if (connecting) return; // повторные клики игнорируем, пока запрос в полёте
    setConnecting(true);
    addLog(`Подключение к ${ssid}...`, 'info');
    try {
      const result = await apiCall('/api/wifi/connect', 'POST', { ssid, password: pass });
      if (result.success) {
        addLog(`Подключено! IP: ${result.ip}`, 'success');
        setSelected(null);
        setPassword('');
      } else {
        addLog(`Ошибка: ${result.error}`, 'error');
      }
    } catch {
      addLog('Ошибка подключения', 'error');
    } finally {
      setConnecting(false);
    }
  };

  const connect = () => {
    if (!selected) return;
    connectTo(selected.ssid, password);
  };

  const handleNetworkClick = (net: WiFiNetwork) => {
    if (connecting) return; // не даём начать новое подключение поверх текущего
    if (net.encryption === 0) {
      // Открытая сеть — подключаемся сразу, тем же путём, что и защищённая.
      setSelected(net);
      setPassword('');
      connectTo(net.ssid, '');
    } else {
      setSelected(net);
      setPassword('');
      setShowPass(false);
    }
  };

  // Уровень сигнала: количество активных дуг и цвет.
  const signalLevel = (rssi: number): 1 | 2 | 3 => (rssi > -55 ? 3 : rssi > -70 ? 2 : 1);
  const signalColor = (rssi: number) =>
    rssi > -50 ? 'var(--green)' : rssi > -70 ? 'var(--amber)' : 'var(--red)';

  return (
    <div>
      <div className="page-title"><IconWifi size={22} /> WiFi</div>

      {/* Текущее подключение */}
      {wifiStatus === 'connected' && wifiInfo && (
        <div className="section">
          <div className="section-header">
            <IconWifi size={14} />
            Текущее подключение
          </div>
          <div className="list-item">
            <span className="list-icon" style={{ color: 'var(--green)', background: 'var(--green-dim)' }}>
              <IconWifiLevel size={18} level={signalLevel(wifiInfo.rssi)} />
            </span>
            <div className="list-item-body">
              <div className="list-item-title">{wifiInfo.ssid}</div>
              <div className="list-item-sub">
                <span className="list-item-meta">{wifiInfo.ip} · {wifiInfo.rssi} dBm</span>
              </div>
            </div>
            <span className="badge badge--green">Подключено</span>
          </div>
        </div>
      )}

      {/* Индикатор идущего подключения (в т.ч. к открытой сети без модалки) */}
      {connecting && (
        <div className="learning-bar" style={{ marginBottom: 12 }}>
          <span className="learning-dot" />
          <span className="learning-text">
            Подключение{selected ? ` к ${selected.ssid}` : ''}...
          </span>
        </div>
      )}

      {/* Доступные сети */}
      <div className="section">
        <div className="section-header">
          <IconWifi size={14} />
          Доступные сети
          <span className="section-header-aux">
            {networks.length > 0 && <span className="badge badge--muted">{networks.length}</span>}
            <button
              className="icon-btn icon-btn--accent"
              style={{ width: 32, height: 32 }}
              onClick={scan}
              disabled={scanning || connecting}
              aria-label="Сканировать сети"
            >
              <IconRefresh size={15} className={scanning ? 'spin' : undefined} />
            </button>
          </span>
        </div>

        {scanning && networks.length === 0 ? (
          <div className="empty">
            <div className="empty-icon"><IconRefresh size={24} className="spin" /></div>
            <div className="empty-title">Сканирование...</div>
          </div>
        ) : networks.length === 0 ? (
          <div className="empty">
            <div className="empty-icon"><IconWifi size={24} /></div>
            <div className="empty-title">Сети не найдены</div>
            <div className="empty-sub">Нажмите кнопку обновления для повторного поиска</div>
          </div>
        ) : (
          networks.map((net, i) => (
            <div
              key={i}
              className="list-item list-item--click"
              onClick={() => handleNetworkClick(net)}
              style={{ cursor: connecting ? 'default' : 'pointer', opacity: connecting ? 0.5 : 1, pointerEvents: connecting ? 'none' : 'auto' }}
            >
              <span className="list-icon" style={{ color: signalColor(net.rssi) }}>
                <IconWifiLevel size={18} level={signalLevel(net.rssi)} />
              </span>
              <div className="list-item-body">
                <div className="list-item-title">{net.ssid || '(скрытая сеть)'}</div>
                <div className="list-item-sub">
                  {net.encryption === 0
                    ? <span className="badge badge--orange"><IconUnlock size={11} /> Открытая</span>
                    : <span className="badge badge--muted"><IconLock size={11} /> Защищённая</span>}
                  <span className="list-item-meta">{net.rssi} dBm</span>
                </div>
              </div>
              <span className="stat-chevron"><IconChevronRight size={18} /></span>
            </div>
          ))
        )}
      </div>

      {/* Password modal */}
      {selected && selected.encryption !== 0 && (
        <div className="modal-overlay" onClick={() => setSelected(null)}>
          <div className="modal" onClick={e => e.stopPropagation()}>
            <div className="modal-head">
              <span className="modal-head-icon"><IconLock size={17} /></span>
              <div className="modal-title">{selected.ssid}</div>
            </div>
            <div className="modal-text">Введите пароль для подключения</div>
            <div className="igroup" style={{ marginBottom: 16 }}>
              <input
                className="input"
                type={showPass ? 'text' : 'password'}
                value={password}
                onChange={e => setPassword(e.target.value)}
                onKeyDown={e => e.key === 'Enter' && password && !connecting && connect()}
                placeholder="Пароль"
                autoFocus
                disabled={connecting}
                aria-label="Пароль сети"
              />
              <button
                className="icon-btn"
                style={{ border: 'none', width: 40 }}
                onClick={() => setShowPass(v => !v)}
                aria-label={showPass ? 'Скрыть пароль' : 'Показать пароль'}
                type="button"
              >
                {showPass ? <IconEyeOff size={16} /> : <IconEye size={16} />}
              </button>
            </div>
            <div className="modal-actions">
              <button className="btn btn--ghost" onClick={() => setSelected(null)} disabled={connecting}>
                Отмена
              </button>
              <button className="btn btn--primary" onClick={connect} disabled={!password || connecting}>
                {connecting ? 'Подключение...' : 'Подключить'}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default WiFiPage;
