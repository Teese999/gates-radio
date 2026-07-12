import React, { useState, useEffect, useCallback, useRef, createContext, useContext } from 'react';
import './App.css';
import WiFiPage from './pages/WiFiPage';
import PhonePage from './pages/PhonePage';
import KeyPage from './pages/KeyPage';
import SettingsPage from './pages/SettingsPage';
import {
  IconHome, IconKey, IconWifi, IconPhone, IconSettings, IconGate, IconChevronLeft,
  IconChevronRight, IconCpu, IconClock, IconActivity, IconZap, IconTerminal, IconTrash,
  IconInfo, IconCheckCircle, IconXCircle, IconAlert,
} from './Icons';

// --- Types ---
interface LogEntry {
  id: number;
  timestamp: number;
  message: string;
  type: 'info' | 'error' | 'success' | 'warning';
}

export interface WifiInfo {
  ssid: string;
  rssi: number;
  ip: string;
}

type Page = 'home' | 'wifi' | 'phones' | 'keys' | 'settings';

type WsEventHandler = (data: any) => void;

type ToastType = 'success' | 'error' | 'info';

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
  wifiInfo: WifiInfo | null;
  wifiStatus: 'connected' | 'disconnected';
  // Обновление счётчиков главной из реального состояния (после add/delete).
  refreshKeyCount: () => void;
  refreshPhoneCount: () => void;
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
  wifiInfo: null,
  wifiStatus: 'disconnected',
  refreshKeyCount: () => {},
  refreshPhoneCount: () => {},
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

const LOG_ICONS: Record<LogEntry['type'], React.ReactNode> = {
  info: <IconInfo size={13} />,
  success: <IconCheckCircle size={13} />,
  error: <IconXCircle size={13} />,
  warning: <IconAlert size={13} />,
};

const TOAST_ICONS: Record<ToastType, React.ReactNode> = {
  success: <IconCheckCircle size={16} />,
  error: <IconXCircle size={16} />,
  info: <IconInfo size={16} />,
};

const GATE_STATUS_TEXT: Record<'closed' | 'opening' | 'open' | 'closing', string> = {
  closed: 'Закрыто',
  opening: 'Открытие...',
  open: 'Открыто',
  closing: 'Закрытие...',
};

// --- App ---
function App() {
  const [connected, setConnected] = useState(false);
  const [wifiStatus, setWifiStatus] = useState<'connected' | 'disconnected'>('disconnected');
  const [wifiInfo, setWifiInfo] = useState<WifiInfo | null>(null);
  const [phoneCount, setPhoneCount] = useState(0);
  const [keyCount, setKeyCount] = useState(0);
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [currentPage, setCurrentPage] = useState<Page>('home');
  const [notification, setNotification] = useState<{ msg: string; type: ToastType } | null>(null);
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
  const showNotification = useCallback((msg: string, type: ToastType = 'info') => {
    setNotification({ msg, type });
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
    // Проверяем HTTP-статус: 4xx/5xx — это ошибка, а не «тихий успех».
    if (!response.ok) {
      let detail = '';
      try {
        const body = await response.json();
        detail = body?.error || body?.message || '';
      } catch {}
      throw new Error(detail || `HTTP ${response.status} ${response.statusText}`);
    }
    return response.json();
  }, []);

  // --- Counters (производные от реального состояния, а не от несуществующих WS-событий) ---
  const refreshKeyCount = useCallback(async () => {
    try {
      const keys = await apiCall('/api/keys');
      setKeyCount(Array.isArray(keys) ? keys.length : 0);
    } catch {}
  }, [apiCall]);

  const refreshPhoneCount = useCallback(async () => {
    try {
      const phones = await apiCall('/api/phones');
      setPhoneCount(Array.isArray(phones) ? phones.length : 0);
    } catch {}
  }, [apiCall]);

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
            case 'key_added':
              // Прошивка реально шлёт это событие — перезапрашиваем счётчик из состояния.
              refreshKeyCount();
              showNotification(`Ключ сохранён: ${data.name || data.protocol}`, 'success');
              addLog(`Ключ сохранён: ${data.name} [${data.protocol}]`, 'success');
              break;
            case 'key_received':
              addLog(`Сигнал: ${data.protocol || 'RAW'}, ${data.bitLength} бит, RSSI ${data.rssi}`, 'info');
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
  }, [addLog, emit, showNotification, startGateCycle, refreshKeyCount]);

  // --- Load initial stats ---
  useEffect(() => {
    refreshPhoneCount();
    refreshKeyCount();
  }, [refreshPhoneCount, refreshKeyCount]);

  // --- Тайминги ворот: источник правды — прошивка (localStorage лишь кэш до ответа) ---
  useEffect(() => {
    (async () => {
      try {
        const cfg = await apiCall('/api/gate/config');
        if (cfg && typeof cfg.openDuration === 'number') {
          const t = { openDuration: cfg.openDuration, stayOpen: cfg.stayOpen, closeDuration: cfg.closeDuration };
          setGateTimingsState(t);
          saveGateTimings(t);
        }
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
      showNotification('Сигнал отправлен', 'success');
      addLog('Сигнал на ворота отправлен', 'success');
      startGateCycle();
    } catch {
      showNotification('Ошибка отправки', 'error');
      addLog('Ошибка отправки сигнала', 'error');
    } finally {
      setTimeout(() => setGateTriggering(false), 1000);
    }
  };

  // --- Context ---
  const ctx: AppContextValue = {
    apiCall, addLog, subscribe, connected, gateStatus, gateTimings, setGateTimings, systemInfo,
    wifiInfo, wifiStatus, refreshKeyCount, refreshPhoneCount,
  };

  // --- Nav ---
  const navItems: { page: Page; label: string; icon: React.ReactNode }[] = [
    { page: 'home', label: 'Главная', icon: <IconHome size={21} /> },
    { page: 'keys', label: 'Ключи', icon: <IconKey size={21} /> },
    { page: 'wifi', label: 'WiFi', icon: <IconWifi size={21} /> },
    { page: 'phones', label: 'Телефоны', icon: <IconPhone size={21} /> },
    { page: 'settings', label: 'Настройки', icon: <IconSettings size={21} /> },
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

  // Створка едет по реальным таймингам цикла ворот.
  const leafOpen = gateStatus === 'opening' || gateStatus === 'open';
  const leafDuration = gateStatus === 'opening'
    ? gateTimings.openDuration
    : gateStatus === 'closing' ? gateTimings.closeDuration : 0.3;

  const renderHome = () => (
    <div className="home">
      <div className="home-col">
        {/* Ворота: статус + визуализация + управление */}
        <div className={`gate-hero gate-hero--${gateStatus}`}>
          <div className="gate-hero-top">
            <div className="gate-hero-title">
              <IconGate size={18} />
              Ворота
            </div>
            <div className={`gate-pill gate-pill--${gateStatus}`}>
              <span className="gate-pill-dot" />
              {GATE_STATUS_TEXT[gateStatus]}
            </div>
          </div>

          <div className="gate-visual" aria-hidden="true">
            <div className="gate-scene">
              <div
                className={`gate-leaf ${leafOpen ? 'gate-leaf--open' : ''}`}
                style={{ transitionDuration: `${leafDuration}s` }}
              >
                {Array.from({ length: 7 }).map((_, i) => <span key={i} className="gate-bar" />)}
              </div>
            </div>
            <div className="gate-ground" />
            <div className="gate-post gate-post--l"><span className="gate-post-lamp" /></div>
            <div className="gate-post gate-post--r"><span className="gate-post-lamp" /></div>
          </div>

          <button
            className={`gate-btn ${gateTriggering ? 'gate-btn--active' : ''}`}
            onClick={triggerGate}
            disabled={gateTriggering}
          >
            {gateTriggering ? <span className="gate-btn-spinner" /> : <IconZap size={18} />}
            {gateTriggering ? 'Отправка...' : 'Открыть ворота'}
          </button>

          <div className="gate-stats">
            <span>Посл. открытие: <b>{timeAgo(lastOpenTime)}</b></span>
            <span>За сессию: <b>{sessionOpenCount}</b></span>
          </div>
        </div>

        {/* Системные показатели */}
        <div className="sys-row">
          <div className="sys-chip" title="Свободная память">
            <IconCpu size={14} />
            <span className="sys-chip-value">{formatBytes(systemInfo.freeHeap)}</span>
            <span className="sys-chip-label">Heap</span>
          </div>
          <div className="sys-chip" title="Время работы">
            <IconClock size={14} />
            <span className="sys-chip-value">{formatUptime(systemInfo.uptime)}</span>
            <span className="sys-chip-label">Uptime</span>
          </div>
          <div className="sys-chip" title="Уровень радиосигнала">
            <IconActivity size={14} />
            <span className="sys-chip-value">{systemInfo.rssi ? `${systemInfo.rssi} dBm` : '—'}</span>
            <span className="sys-chip-label">RSSI</span>
          </div>
          <div className="sys-chip" title="Всего открытий">
            <IconZap size={14} />
            <span className="sys-chip-value">{systemInfo.openCount}</span>
            <span className="sys-chip-label">Открытий</span>
          </div>
        </div>
      </div>

      <div className="home-col">
        {/* Карточки-ссылки */}
        <div className="stat-cards">
          <button className="stat-card" onClick={() => setCurrentPage('keys')}>
            <span className="stat-icon"><IconKey size={20} /></span>
            <span className="stat-body">
              <span className="stat-value">{keyCount}</span>
              <div className="stat-label">Ключей</div>
            </span>
            <span className="stat-chevron"><IconChevronRight size={18} /></span>
          </button>

          <button className="stat-card" onClick={() => setCurrentPage('wifi')}>
            <span className={`stat-icon ${wifiStatus === 'connected' ? 'stat-icon--green' : 'stat-icon--red'}`}>
              <IconWifi size={20} />
            </span>
            <span className="stat-body">
              <span className="stat-value">{wifiInfo ? wifiInfo.ssid : 'Не подключено'}</span>
              <div className="stat-label">{wifiInfo ? wifiInfo.ip : 'WiFi'}</div>
            </span>
            <span className="stat-chevron"><IconChevronRight size={18} /></span>
          </button>

          <button className="stat-card" onClick={() => setCurrentPage('phones')}>
            <span className="stat-icon"><IconPhone size={20} /></span>
            <span className="stat-body">
              <span className="stat-value">{phoneCount}</span>
              <div className="stat-label">Телефонов</div>
            </span>
            <span className="stat-chevron"><IconChevronRight size={18} /></span>
          </button>
        </div>

        {/* Журнал */}
        <div className="console">
          <div className="console-bar">
            <span className="console-title">
              <IconTerminal size={14} />
              Журнал
            </span>
            <button className="console-clear" onClick={() => setLogs([])}>
              <IconTrash size={12} />
              Очистить
            </button>
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
                  <span className="console-icon">{LOG_ICONS[log.type]}</span>
                  <span className="console-msg">{log.message}</span>
                </div>
              ))
            )}
          </div>
        </div>
      </div>
    </div>
  );

  return (
    <AppContext.Provider value={ctx}>
      <div className="app">
        {/* Шапка (мобайл) */}
        <header className="header">
          {currentPage !== 'home' ? (
            <button className="header-back" onClick={() => setCurrentPage('home')} aria-label="Назад">
              <IconChevronLeft size={18} />
            </button>
          ) : (
            <div className="header-spacer" />
          )}
          <div className="header-brand">
            {currentPage === 'home' && <span className="header-brand-icon"><IconGate size={19} /></span>}
            <h1 className="header-title">{PAGE_TITLES[currentPage]}</h1>
          </div>
          <div className={`header-status ${connected ? 'header-status--on' : 'header-status--off'}`}>
            <span className="header-status-dot" />
            {connected ? 'Online' : 'Offline'}
          </div>
        </header>

        <main className="main">
          {renderPage()}
        </main>

        {/* Навигация: нижняя панель (мобайл) / сайдбар (десктоп) */}
        <nav className="nav">
          <div className="nav-brand">
            <span className="nav-brand-icon"><IconGate size={20} /></span>
            <span>
              <div className="nav-brand-name">SmartGate</div>
              <div className="nav-brand-sub">{systemInfo.firmware || 'v1.0'}</div>
            </span>
          </div>

          {navItems.map(item => (
            <button
              key={item.page}
              className={`nav-item ${currentPage === item.page ? 'nav-item--active' : ''}`}
              onClick={() => setCurrentPage(item.page)}
            >
              {item.icon}
              <span className="nav-label">{item.label}</span>
            </button>
          ))}

          <div className="nav-foot">
            <div className={`nav-foot-status ${connected ? 'nav-foot-status--on' : 'nav-foot-status--off'}`}>
              <span className="header-status-dot" />
              {connected ? 'Устройство на связи' : 'Нет связи'}
            </div>
            <div className="nav-foot-meta">
              uptime {formatUptime(systemInfo.uptime)} · heap {formatBytes(systemInfo.freeHeap)}
            </div>
          </div>
        </nav>

        {notification && (
          <div className={`toast toast--${notification.type}`} onClick={() => setNotification(null)}>
            {TOAST_ICONS[notification.type]}
            <span>{notification.msg}</span>
          </div>
        )}
      </div>
    </AppContext.Provider>
  );
}

export default App;
