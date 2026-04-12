import React, { useState, useEffect, useCallback, useRef, createContext, useContext } from 'react';
import './App.css';
import WiFiPage from './pages/WiFiPage';
import PhonePage from './pages/PhonePage';
import KeyPage from './pages/KeyPage';
import SettingsPage from './pages/SettingsPage';

// --- Types ---
interface LogEntry {
  id: number;
  timestamp: number;
  message: string;
  type: 'info' | 'error' | 'success' | 'warning';
}

interface WifiInfo {
  ssid: string;
  rssi: number;
  ip: string;
}

type Page = 'home' | 'wifi' | 'phones' | 'keys' | 'settings';

type WsEventHandler = (data: any) => void;

interface GateTimings {
  openDuration: number;
  stayOpen: number;
  closeDuration: number;
}

interface SystemInfo {
  uptime: number;
  freeHeap: number;
  totalHeap: number;
  rssi: number;
  firmware: string;
  openCount: number;
}

const PAGE_TITLES: Record<Page, string> = {
  home: 'SmartGate',
  keys: 'Ключи',
  wifi: 'WiFi',
  phones: 'Телефоны',
  settings: 'Настройки',
};

const GATE_TIMINGS_KEY = 'smartgate_timings';

const loadGateTimings = (): GateTimings => {
  try {
    const saved = localStorage.getItem(GATE_TIMINGS_KEY);
    if (saved) return JSON.parse(saved);
  } catch {}
  return { openDuration: 3, stayOpen: 15, closeDuration: 3 };
};

const saveGateTimings = (t: GateTimings) => {
  localStorage.setItem(GATE_TIMINGS_KEY, JSON.stringify(t));
};

export interface AppContextValue {
  apiCall: (endpoint: string, method?: string, data?: any) => Promise<any>;
  addLog: (message: string, type?: LogEntry['type']) => void;
  subscribe: (event: string, handler: WsEventHandler) => () => void;
  connected: boolean;
  gateStatus: 'closed' | 'opening' | 'open' | 'closing';
  gateTimings: GateTimings;
  setGateTimings: (t: GateTimings) => void;
  systemInfo: SystemInfo;
}

// --- Context ---
export const AppContext = createContext<AppContextValue>({
  apiCall: async () => ({}),
  addLog: () => {},
  subscribe: () => () => {},
  connected: false,
  gateStatus: 'closed',
  gateTimings: { openDuration: 3, stayOpen: 15, closeDuration: 3 },
  setGateTimings: () => {},
  systemInfo: { uptime: 0, freeHeap: 0, totalHeap: 0, rssi: 0, firmware: '', openCount: 0 },
});

export const useApp = () => useContext(AppContext);

// --- Constants ---
const BASE_URL = window.location.hostname === 'localhost'
  ? 'http://192.168.4.1'
  : `http://${window.location.hostname}`;

const WS_URL = window.location.hostname === 'localhost'
  ? 'ws://192.168.4.1:81'
  : `ws://${window.location.hostname}:81`;

let logIdCounter = 0;

// --- Helpers ---
function formatUptime(seconds: number): string {
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (d > 0) return `${d}д ${h}ч`;
  if (h > 0) return `${h}ч ${m}м`;
  return `${m}м`;
}

function formatBytes(bytes: number): string {
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${bytes} B`;
}

function timeAgo(ts: number): string {
  if (!ts) return 'никогда';
  const diff = Math.floor((Date.now() - ts) / 1000);
  if (diff < 5) return 'только что';
  if (diff < 60) return `${diff} сек назад`;
  if (diff < 3600) return `${Math.floor(diff / 60)} мин назад`;
  if (diff < 86400) return `${Math.floor(diff / 3600)} ч назад`;
  return `${Math.floor(diff / 86400)} дн назад`;
}

// --- App ---
function App() {
  const [connected, setConnected] = useState(false);
  const [wifiStatus, setWifiStatus] = useState<'connected' | 'disconnected'>('disconnected');
  const [wifiInfo, setWifiInfo] = useState<WifiInfo | null>(null);
  const [phoneCount, setPhoneCount] = useState(0);
  const [keyCount, setKeyCount] = useState(0);
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [currentPage, setCurrentPage] = useState<Page>('home');
  const [notification, setNotification] = useState<string | null>(null);
  const [gateTriggering, setGateTriggering] = useState(false);
  const [gateStatus, setGateStatus] = useState<'closed' | 'opening' | 'open' | 'closing'>('closed');
  const [gateTimings, setGateTimingsState] = useState<GateTimings>(loadGateTimings);
  const [lastOpenTime, setLastOpenTime] = useState(0);
  const [sessionOpenCount, setSessionOpenCount] = useState(0);
  const [systemInfo, setSystemInfo] = useState<SystemInfo>({
    uptime: 0, freeHeap: 0, totalHeap: 0, rssi: 0, firmware: 'v1.0', openCount: 0,
  });

  const setGateTimings = useCallback((t: GateTimings) => {
    setGateTimingsState(t);
    saveGateTimings(t);
  }, []);

  const subscribersRef = useRef<Map<string, Set<WsEventHandler>>>(new Map());
  const wsRef = useRef<WebSocket | null>(null);
  const notificationTimerRef = useRef<NodeJS.Timeout | null>(null);
  const gateTimerRef = useRef<NodeJS.Timeout | null>(null);

  const gateTimingsRef = useRef(gateTimings);
  useEffect(() => { gateTimingsRef.current = gateTimings; }, [gateTimings]);

  // --- Gate status cycle ---
  const startGateCycle = useCallback(() => {
    if (gateTimerRef.current) clearTimeout(gateTimerRef.current);
    const t = gateTimingsRef.current;

    setLastOpenTime(Date.now());
    setSessionOpenCount(prev => prev + 1);
    setGateStatus('opening');
    gateTimerRef.current = setTimeout(() => {
      setGateStatus('open');
      gateTimerRef.current = setTimeout(() => {
        setGateStatus('closing');
        gateTimerRef.current = setTimeout(() => {
          setGateStatus('closed');
          gateTimerRef.current = null;
        }, t.closeDuration * 1000);
      }, t.stayOpen * 1000);
    }, t.openDuration * 1000);
  }, []);

  useEffect(() => {
    return () => { if (gateTimerRef.current) clearTimeout(gateTimerRef.current); };
  }, []);

  // --- Notification ---
  const showNotification = useCallback((msg: string) => {
    setNotification(msg);
    if (notificationTimerRef.current) clearTimeout(notificationTimerRef.current);
    notificationTimerRef.current = setTimeout(() => setNotification(null), 3000);
  }, []);

  // --- Logging ---
  const addLog = useCallback((message: string, type: LogEntry['type'] = 'info') => {
    setLogs(prev => [{
      id: ++logIdCounter,
      timestamp: Date.now(),
      message,
      type,
    }, ...prev.slice(0, 99)]);
  }, []);

  // --- API ---
  const apiCall = useCallback(async (endpoint: string, method = 'GET', data: any = null) => {
    const response = await fetch(`${BASE_URL}${endpoint}`, {
      method,
      headers: { 'Content-Type': 'application/json' },
      body: data ? JSON.stringify(data) : null,
    });
    return response.json();
  }, []);

  // --- PubSub ---
  const subscribe = useCallback((event: string, handler: WsEventHandler) => {
    if (!subscribersRef.current.has(event)) {
      subscribersRef.current.set(event, new Set());
    }
    subscribersRef.current.get(event)!.add(handler);
    return () => { subscribersRef.current.get(event)?.delete(handler); };
  }, []);

  const emit = useCallback((event: string, data: any) => {
    subscribersRef.current.get(event)?.forEach(handler => handler(data));
  }, []);

  // --- WebSocket ---
  useEffect(() => {
    let reconnectTimer: NodeJS.Timeout | null = null;
    let alive = true;

    const connect = () => {
      if (!alive) return;
      const ws = new WebSocket(WS_URL);
      wsRef.current = ws;

      ws.onopen = () => {
        setConnected(true);
        addLog('Подключено к устройству', 'success');
      };

      ws.onclose = () => {
        setConnected(false);
        wsRef.current = null;
        if (alive) reconnectTimer = setTimeout(connect, 3000);
      };

      ws.onerror = () => {};

      ws.onmessage = (ev) => {
        try {
          const msg = JSON.parse(ev.data);
          const { event, data } = msg;

          switch (event) {
            case 'log':
              addLog(data.message, data.type || 'info');
              if (data.type === 'success' && data.message?.toLowerCase().includes('ворота активированы')) {
                startGateCycle();
              }
              break;
            case 'wifi_status':
              setWifiStatus(data.status);
              if (data.status === 'connected' && data.ssid) {
                setWifiInfo({ ssid: data.ssid, rssi: data.rssi, ip: data.ip });
              } else {
                setWifiInfo(null);
              }
              break;
            case 'phone_count': setPhoneCount(data.count); break;
            case 'key_count': setKeyCount(data.count); break;
            case 'key_added':
              setKeyCount(prev => prev + 1);
              showNotification(`Ключ сохранён: ${data.name || data.protocol}`);
              addLog(`Ключ сохранён: ${data.name} [${data.protocol}]`, 'success');
              break;
            case 'key_received':
              addLog(`Сигнал: ${data.protocol || 'RAW'}, ${data.bitLength} бит, RSSI ${data.rssi}`, 'info');
              break;
            case 'gate_triggered':
              startGateCycle();
              showNotification(`Ворота: ${data.keyName || 'активация'}`);
              break;
          }

          emit(event, data);
        } catch {}
      };
    };

    connect();
    return () => {
      alive = false;
      if (reconnectTimer) clearTimeout(reconnectTimer);
      wsRef.current?.close();
    };
  }, [addLog, emit, showNotification, startGateCycle]);

  // --- Load initial stats ---
  useEffect(() => {
    (async () => {
      try {
        const [phones, keys] = await Promise.all([
          apiCall('/api/phones'),
          apiCall('/api/keys'),
        ]);
        setPhoneCount(Array.isArray(phones) ? phones.length : 0);
        setKeyCount(Array.isArray(keys) ? keys.length : 0);
      } catch {}
    })();
  }, [apiCall]);

  // --- Poll system info every 10s ---
  useEffect(() => {
    const fetchInfo = async () => {
      try {
        const info = await apiCall('/api/system/info');
        if (info) setSystemInfo(prev => ({ ...prev, ...info }));
      } catch {}
    };
    fetchInfo();
    const interval = setInterval(fetchInfo, 10000);
    return () => clearInterval(interval);
  }, [apiCall]);

  // --- Gate trigger ---
  const triggerGate = async () => {
    if (gateTriggering) return;
    setGateTriggering(true);
    try {
      await apiCall('/api/gate/trigger', 'POST');
      showNotification('Сигнал отправлен');
      addLog('Сигнал на ворота отправлен', 'success');
      startGateCycle();
    } catch {
      showNotification('Ошибка отправки');
      addLog('Ошибка отправки сигнала', 'error');
    } finally {
      setTimeout(() => setGateTriggering(false), 1000);
    }
  };

  // --- Context ---
  const ctx: AppContextValue = {
    apiCall, addLog, subscribe, connected, gateStatus, gateTimings, setGateTimings, systemInfo,
  };

  // --- Nav ---
  const navItems: { page: Page; label: string; icon: string }[] = [
    { page: 'home', label: 'Главная', icon: 'home' },
    { page: 'keys', label: 'Ключи', icon: 'key' },
    { page: 'wifi', label: 'WiFi', icon: 'wifi' },
    { page: 'phones', label: 'Телефоны', icon: 'phone' },
    { page: 'settings', label: 'Настройки', icon: 'settings' },
  ];

  const renderPage = () => {
    switch (currentPage) {
      case 'wifi': return <WiFiPage />;
      case 'phones': return <PhonePage />;
      case 'keys': return <KeyPage />;
      case 'settings': return <SettingsPage />;
      default: return renderHome();
    }
  };

  const renderHome = () => (
    <>
      {/* Status row */}
      <div className="status-row">
        <div className="status-chip">
          <span className="status-chip-label">Heap</span>
          <span className="status-chip-value">{formatBytes(systemInfo.freeHeap)}</span>
        </div>
        <div className="status-chip">
          <span className="status-chip-label">Uptime</span>
          <span className="status-chip-value">{formatUptime(systemInfo.uptime)}</span>
        </div>
        <div className="status-chip">
          <span className="status-chip-label">RSSI</span>
          <span className="status-chip-value">{systemInfo.rssi || '—'} dBm</span>
        </div>
      </div>

      {/* Cards */}
      <div className="cards">
        <div className="card" onClick={() => setCurrentPage('keys')}>
          <div className="card-value">{keyCount}</div>
          <div className="card-label">Ключей</div>
        </div>
        <div className="card" onClick={() => setCurrentPage('wifi')}>
          <div className={`card-dot ${wifiStatus === 'connected' ? 'dot-green' : 'dot-red'}`} />
          <div className="card-label">{wifiInfo ? wifiInfo.ssid : 'WiFi'}</div>
          {wifiInfo && <div className="card-sub">{wifiInfo.ip}</div>}
        </div>
        <div className="card" onClick={() => setCurrentPage('phones')}>
          <div className="card-value">{phoneCount}</div>
          <div className="card-label">Телефонов</div>
        </div>
      </div>

      {/* Gate */}
      <div className="gate-section">
        <div className={`gate-status gate-status--${gateStatus}`}>
          <span className="gate-status-dot" />
          <span className="gate-status-text">{
            gateStatus === 'closed' ? 'Закрыто' :
            gateStatus === 'opening' ? 'Открытие...' :
            gateStatus === 'open' ? 'Открыто' : 'Закрытие...'
          }</span>
        </div>
        <button
          className={`gate-btn ${gateTriggering ? 'gate-btn--active' : ''}`}
          onClick={triggerGate}
          disabled={gateTriggering}
        >
          {gateTriggering ? 'Отправка...' : 'Открыть ворота'}
        </button>
        {(lastOpenTime > 0 || sessionOpenCount > 0) && (
          <div className="gate-stats">
            {lastOpenTime > 0 && <span>Посл. открытие: {timeAgo(lastOpenTime)}</span>}
            {sessionOpenCount > 0 && <span>За сессию: {sessionOpenCount}</span>}
          </div>
        )}
      </div>

      {/* Console */}
      <div className="console">
        <div className="console-bar">
          <span className="console-title">Журнал</span>
          <button className="console-clear" onClick={() => setLogs([])}>Очистить</button>
        </div>
        <div className="console-body">
          {logs.length === 0 ? (
            <div className="console-empty">Нет записей</div>
          ) : (
            logs.map(log => (
              <div key={log.id} className={`console-line console-line--${log.type}`}>
                <span className="console-time">
                  {new Date(log.timestamp).toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit', second: '2-digit' })}
                </span>
                <span className="console-msg">{log.message}</span>
              </div>
            ))
          )}
        </div>
      </div>
    </>
  );

  return (
    <AppContext.Provider value={ctx}>
      <div className="app">
        {/* Header */}
        <header className="header">
          {currentPage !== 'home' ? (
            <button className="header-back" onClick={() => setCurrentPage('home')}>
              <span className="header-back-arrow" />
            </button>
          ) : (
            <div style={{ width: 32 }} />
          )}
          <h1 className="header-title">{PAGE_TITLES[currentPage]}</h1>
          <div className={`header-status ${connected ? 'header-status--on' : 'header-status--off'}`}>
            <span className="header-status-dot" />
            {connected ? 'Online' : 'Offline'}
          </div>
        </header>

        <main className="main">
          {renderPage()}
        </main>

        <nav className="nav">
          {navItems.map(item => (
            <button
              key={item.page}
              className={`nav-item ${currentPage === item.page ? 'nav-item--active' : ''}`}
              onClick={() => setCurrentPage(item.page)}
            >
              <span className={`nav-icon nav-icon--${item.icon}`} />
              <span className="nav-label">{item.label}</span>
            </button>
          ))}
        </nav>

        {notification && (
          <div className="toast" onClick={() => setNotification(null)}>
            {notification}
          </div>
        )}
      </div>
    </AppContext.Provider>
  );
}

export default App;
