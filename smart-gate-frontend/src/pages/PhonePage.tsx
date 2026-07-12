import React, { useState, useEffect, useCallback } from 'react';
import { useApp } from '../App';
import { IconPhone, IconPlus, IconTrash, IconMessage, IconCall, IconAlert } from '../Icons';

interface Phone {
  id: string;
  number: string;
  smsEnabled: boolean;
  callEnabled: boolean;
}

// "9001234567" → "900 123-45-67" (частичный ввод форматируется по мере набора)
const formatDigits = (d: string) => {
  let out = d.slice(0, 3);
  if (d.length > 3) out += ' ' + d.slice(3, 6);
  if (d.length > 6) out += '-' + d.slice(6, 8);
  if (d.length > 8) out += '-' + d.slice(8, 10);
  return out;
};

// "+79001234567" → "+7 900 123-45-67"
const formatFull = (num: string) => {
  const digits = num.replace(/\D/g, '').slice(-10);
  return digits.length === 10 ? `+7 ${formatDigits(digits)}` : num;
};

const PhonePage: React.FC = () => {
  const { apiCall, addLog, refreshPhoneCount } = useApp();
  const [phones, setPhones] = useState<Phone[]>([]);
  const [loading, setLoading] = useState(true);
  const [newPhone, setNewPhone] = useState(''); // только цифры, до 10
  const [deleteTarget, setDeleteTarget] = useState<string | null>(null);

  const loadPhones = useCallback(async () => {
    try {
      const data = await apiCall('/api/phones');
      if (Array.isArray(data)) setPhones(data);
    } catch {
      addLog('Ошибка загрузки телефонов', 'error');
    } finally {
      setLoading(false);
    }
  }, [apiCall, addLog]);

  useEffect(() => { loadPhones(); }, [loadPhones]);

  const addPhone = async () => {
    const digits = newPhone.replace(/\D/g, '');
    if (digits.length !== 10) {
      addLog('Номер должен содержать 10 цифр', 'warning');
      return;
    }
    const fullNumber = `+7${digits}`;
    // id == номер, поэтому дубликаты рассинхронизируют список при удалении — блокируем заранее.
    if (phones.some(p => p.number === fullNumber)) {
      addLog(`Телефон ${fullNumber} уже добавлен`, 'warning');
      return;
    }
    try {
      const data = await apiCall('/api/phones', 'POST', {
        number: fullNumber,
        smsEnabled: true,
        callEnabled: true,
      });
      setPhones(prev => [...prev, data]);
      refreshPhoneCount(); // счётчик на главной — из реального состояния
      setNewPhone('');
      addLog(`Телефон ${fullNumber} добавлен`, 'success');
    } catch {
      addLog('Ошибка добавления телефона', 'error');
    }
  };

  const deletePhone = async (id: string) => {
    try {
      await apiCall('/api/phones/delete', 'POST', { id });
      setPhones(prev => prev.filter(p => p.id !== id));
      refreshPhoneCount(); // счётчик на главной — из реального состояния
      addLog('Телефон удалён', 'success');
    } catch {
      addLog('Ошибка удаления', 'error');
    } finally {
      setDeleteTarget(null);
    }
  };

  const toggleSms = async (id: string, enabled: boolean) => {
    try {
      await apiCall('/api/phones/update', 'PUT', { id, smsEnabled: enabled });
      setPhones(prev => prev.map(p => p.id === id ? { ...p, smsEnabled: enabled } : p));
    } catch {
      addLog('Ошибка обновления SMS', 'error');
    }
  };

  const toggleCall = async (id: string, enabled: boolean) => {
    try {
      await apiCall('/api/phones/update', 'PUT', { id, callEnabled: enabled });
      setPhones(prev => prev.map(p => p.id === id ? { ...p, callEnabled: enabled } : p));
    } catch {
      addLog('Ошибка обновления звонков', 'error');
    }
  };

  const digitsOk = newPhone.replace(/\D/g, '').length === 10;

  return (
    <div>
      <div className="page-title"><IconPhone size={22} /> Телефоны</div>

      {/* Добавление номера */}
      <div className="section">
        <div className="section-header">
          <IconPlus size={14} />
          Добавить номер
        </div>
        <div className="section-pad">
          <div style={{ display: 'flex', gap: 8 }}>
            <div className="igroup">
              <span className="igroup-prefix">+7</span>
              <input
                className="input input--mono"
                type="tel"
                inputMode="numeric"
                autoComplete="tel-national"
                value={formatDigits(newPhone)}
                onChange={e => {
                  const v = e.target.value.replace(/\D/g, '');
                  if (v.length <= 10) setNewPhone(v);
                }}
                onKeyDown={e => e.key === 'Enter' && addPhone()}
                placeholder="900 123-45-67"
                maxLength={13}
                aria-label="Номер телефона без +7"
              />
            </div>
            <button className="btn btn--primary" onClick={addPhone} disabled={!digitsOk}>
              <IconPlus size={16} />
              Добавить
            </button>
          </div>
          <div style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 8, lineHeight: 1.5 }}>
            Номер сможет открывать ворота звонком или SMS
          </div>
        </div>
      </div>

      {/* Список номеров */}
      {loading ? (
        <div className="section">
          <div className="empty"><div className="empty-title">Загрузка...</div></div>
        </div>
      ) : phones.length === 0 ? (
        <div className="section">
          <div className="empty">
            <div className="empty-icon"><IconPhone size={24} /></div>
            <div className="empty-title">Нет телефонов</div>
            <div className="empty-sub">Добавьте номер, чтобы открывать ворота звонком или SMS</div>
          </div>
        </div>
      ) : (
        <div className="section">
          <div className="section-header">
            <IconPhone size={14} />
            Доверенные номера
            <span className="section-header-aux">
              <span className="badge badge--muted">{phones.length}</span>
            </span>
          </div>
          {phones.map(phone => (
            <div key={phone.id} className="list-item">
              <span className="list-icon"><IconPhone size={18} /></span>
              <div className="list-item-body">
                <div className="list-item-title" style={{ fontFamily: 'var(--mono)' }}>
                  {formatFull(phone.number)}
                </div>
                <div className="list-item-sub">
                  {/* Чип — и индикатор, и переключатель */}
                  <button
                    className={`chip-toggle ${phone.smsEnabled ? 'chip-toggle--on' : ''}`}
                    onClick={() => toggleSms(phone.id, !phone.smsEnabled)}
                    aria-pressed={phone.smsEnabled}
                    aria-label={`SMS для ${phone.number}: ${phone.smsEnabled ? 'включено' : 'выключено'}`}
                  >
                    <IconMessage size={13} />
                    SMS
                  </button>
                  <button
                    className={`chip-toggle ${phone.callEnabled ? 'chip-toggle--on' : ''}`}
                    onClick={() => toggleCall(phone.id, !phone.callEnabled)}
                    aria-pressed={phone.callEnabled}
                    aria-label={`Звонок для ${phone.number}: ${phone.callEnabled ? 'включён' : 'выключен'}`}
                  >
                    <IconCall size={13} />
                    Звонок
                  </button>
                </div>
              </div>
              <div className="list-actions">
                <button
                  className="icon-btn icon-btn--danger"
                  onClick={() => setDeleteTarget(phone.id)}
                  aria-label={`Удалить номер ${phone.number}`}
                >
                  <IconTrash size={15} />
                </button>
              </div>
            </div>
          ))}
        </div>
      )}

      {/* Delete modal */}
      {deleteTarget && (
        <div className="modal-overlay" onClick={() => setDeleteTarget(null)}>
          <div className="modal" onClick={e => e.stopPropagation()}>
            <div className="modal-head">
              <span className="modal-head-icon modal-head-icon--danger"><IconAlert size={18} /></span>
              <div className="modal-title">Удалить телефон?</div>
            </div>
            <div className="modal-text">
              {formatFull(phones.find(p => p.id === deleteTarget)?.number || '')} больше не сможет
              открывать ворота.
            </div>
            <div className="modal-actions">
              <button className="btn btn--ghost" onClick={() => setDeleteTarget(null)}>Отмена</button>
              <button className="btn btn--danger" onClick={() => deletePhone(deleteTarget)}>
                <IconTrash size={15} />
                Удалить
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default PhonePage;
