import React from 'react';

// Единый набор inline-SVG иконок (стиль Lucide, stroke 2).
// Без внешних зависимостей и эмодзи — устройство работает офлайн.

export interface IconProps {
  size?: number;
  strokeWidth?: number;
  className?: string;
}

const base = (size: number, strokeWidth: number, className?: string) => ({
  width: size,
  height: size,
  viewBox: '0 0 24 24',
  fill: 'none',
  stroke: 'currentColor',
  strokeWidth,
  strokeLinecap: 'round' as const,
  strokeLinejoin: 'round' as const,
  className,
  'aria-hidden': true,
});

export const IconHome: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M3 9l9-7 9 7v11a2 2 0 01-2 2H5a2 2 0 01-2-2z" />
    <polyline points="9 22 9 12 15 12 15 22" />
  </svg>
);

export const IconKey: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M21 2l-2 2m-7.61 7.61a5.5 5.5 0 11-7.778 7.778 5.5 5.5 0 017.777-7.777zm0 0L15.5 7.5m0 0l3 3L22 7l-3-3m-3.5 3.5L19 4" />
  </svg>
);

export const IconWifi: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M5 12.55a11 11 0 0114.08 0" />
    <path d="M1.42 9a16 16 0 0121.16 0" />
    <path d="M8.53 16.11a6 6 0 016.95 0" />
    <line x1="12" y1="20" x2="12.01" y2="20" />
  </svg>
);

export const IconPhone: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <rect x="5" y="2" width="14" height="20" rx="2" ry="2" />
    <line x1="12" y1="18" x2="12.01" y2="18" />
  </svg>
);

export const IconSettings: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <line x1="4" y1="21" x2="4" y2="14" />
    <line x1="4" y1="10" x2="4" y2="3" />
    <line x1="12" y1="21" x2="12" y2="12" />
    <line x1="12" y1="8" x2="12" y2="3" />
    <line x1="20" y1="21" x2="20" y2="16" />
    <line x1="20" y1="12" x2="20" y2="3" />
    <line x1="1" y1="14" x2="7" y2="14" />
    <line x1="9" y1="8" x2="15" y2="8" />
    <line x1="17" y1="16" x2="23" y2="16" />
  </svg>
);

export const IconChevronLeft: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <polyline points="15 18 9 12 15 6" />
  </svg>
);

export const IconChevronRight: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <polyline points="9 18 15 12 9 6" />
  </svg>
);

export const IconTrash: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <polyline points="3 6 5 6 21 6" />
    <path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2" />
    <line x1="10" y1="11" x2="10" y2="17" />
    <line x1="14" y1="11" x2="14" y2="17" />
  </svg>
);

export const IconPencil: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M17 3a2.828 2.828 0 114 4L7.5 20.5 2 22l1.5-5.5z" />
  </svg>
);

export const IconPlus: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <line x1="12" y1="5" x2="12" y2="19" />
    <line x1="5" y1="12" x2="19" y2="12" />
  </svg>
);

export const IconX: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <line x1="18" y1="6" x2="6" y2="18" />
    <line x1="6" y1="6" x2="18" y2="18" />
  </svg>
);

export const IconCheck: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <polyline points="20 6 9 17 4 12" />
  </svg>
);

export const IconLock: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <rect x="3" y="11" width="18" height="11" rx="2" ry="2" />
    <path d="M7 11V7a5 5 0 0110 0v4" />
  </svg>
);

export const IconUnlock: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <rect x="3" y="11" width="18" height="11" rx="2" ry="2" />
    <path d="M7 11V7a5 5 0 019.9-1" />
  </svg>
);

export const IconMessage: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M21 15a2 2 0 01-2 2H7l-4 4V5a2 2 0 012-2h14a2 2 0 012 2z" />
  </svg>
);

export const IconCall: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M22 16.92v3a2 2 0 01-2.18 2 19.79 19.79 0 01-8.63-3.07 19.5 19.5 0 01-6-6 19.79 19.79 0 01-3.07-8.67A2 2 0 014.11 2h3a2 2 0 012 1.72 12.84 12.84 0 00.7 2.81 2 2 0 01-.45 2.11L8.09 9.91a16 16 0 006 6l1.27-1.27a2 2 0 012.11-.45 12.84 12.84 0 002.81.7A2 2 0 0122 16.92z" />
  </svg>
);

export const IconRadio: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <circle cx="12" cy="12" r="2" />
    <path d="M16.24 7.76a6 6 0 010 8.49m-8.48-.01a6 6 0 010-8.49m11.31-2.82a10 10 0 010 14.14m-14.14 0a10 10 0 010-14.14" />
  </svg>
);

export const IconClock: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <circle cx="12" cy="12" r="10" />
    <polyline points="12 6 12 12 16 14" />
  </svg>
);

export const IconCpu: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <rect x="4" y="4" width="16" height="16" rx="2" ry="2" />
    <rect x="9" y="9" width="6" height="6" />
    <line x1="9" y1="1" x2="9" y2="4" />
    <line x1="15" y1="1" x2="15" y2="4" />
    <line x1="9" y1="20" x2="9" y2="23" />
    <line x1="15" y1="20" x2="15" y2="23" />
    <line x1="20" y1="9" x2="23" y2="9" />
    <line x1="20" y1="14" x2="23" y2="14" />
    <line x1="1" y1="9" x2="4" y2="9" />
    <line x1="1" y1="14" x2="4" y2="14" />
  </svg>
);

export const IconActivity: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <polyline points="22 12 18 12 15 21 9 3 6 12 2 12" />
  </svg>
);

export const IconZap: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2" />
  </svg>
);

export const IconRefresh: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <polyline points="23 4 23 10 17 10" />
    <path d="M20.49 15a9 9 0 11-2.12-9.36L23 10" />
  </svg>
);

export const IconAlert: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z" />
    <line x1="12" y1="9" x2="12" y2="13" />
    <line x1="12" y1="17" x2="12.01" y2="17" />
  </svg>
);

export const IconInfo: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <circle cx="12" cy="12" r="10" />
    <line x1="12" y1="16" x2="12" y2="12" />
    <line x1="12" y1="8" x2="12.01" y2="8" />
  </svg>
);

export const IconCheckCircle: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M22 11.08V12a10 10 0 11-5.93-9.14" />
    <polyline points="22 4 12 14.01 9 11.01" />
  </svg>
);

export const IconXCircle: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <circle cx="12" cy="12" r="10" />
    <line x1="15" y1="9" x2="9" y2="15" />
    <line x1="9" y1="9" x2="15" y2="15" />
  </svg>
);

export const IconEye: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z" />
    <circle cx="12" cy="12" r="3" />
  </svg>
);

export const IconEyeOff: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M17.94 17.94A10.07 10.07 0 0112 20c-7 0-11-8-11-8a18.45 18.45 0 015.06-5.94M9.9 4.24A9.12 9.12 0 0112 4c7 0 11 8 11 8a18.5 18.5 0 01-2.16 3.19m-6.72-1.07a3 3 0 11-4.24-4.24" />
    <line x1="1" y1="1" x2="23" y2="23" />
  </svg>
);

export const IconMinus: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <line x1="5" y1="12" x2="19" y2="12" />
  </svg>
);

export const IconTerminal: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <polyline points="4 17 10 11 4 5" />
    <line x1="12" y1="19" x2="20" y2="19" />
  </svg>
);

export const IconPower: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M18.36 6.64a9 9 0 11-12.73 0" />
    <line x1="12" y1="2" x2="12" y2="12" />
  </svg>
);

// Уровень WiFi-сигнала: 0..3 активных дуги (по RSSI).
export const IconWifiLevel: React.FC<IconProps & { level: 0 | 1 | 2 | 3 }> = ({
  size = 22, strokeWidth = 2, className, level,
}) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M1.42 9a16 16 0 0121.16 0" opacity={level >= 3 ? 1 : 0.22} />
    <path d="M5 12.55a11 11 0 0114.08 0" opacity={level >= 2 ? 1 : 0.22} />
    <path d="M8.53 16.11a6 6 0 016.95 0" opacity={level >= 1 ? 1 : 0.22} />
    <line x1="12" y1="20" x2="12.01" y2="20" />
  </svg>
);

// Логотип: стилизованные откатные ворота.
export const IconGate: React.FC<IconProps> = ({ size = 22, strokeWidth = 2, className }) => (
  <svg {...base(size, strokeWidth, className)}>
    <path d="M3 21V8l9-5 9 5v13" />
    <line x1="3" y1="21" x2="21" y2="21" />
    <line x1="7.5" y1="11" x2="7.5" y2="17" />
    <line x1="12" y1="11" x2="12" y2="17" />
    <line x1="16.5" y1="11" x2="16.5" y2="17" />
  </svg>
);
