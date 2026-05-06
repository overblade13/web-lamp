const DEVICE_ID = 'b1a0fc95-8236-43be-af6a-cb315d05d75f';
const API_URL = 'http://localhost:3000/api';

// Состояние
let state = {
    is_powered: false,
    is_auto_mode: true,
    brightness: 5
};

// Время последнего изменения пользователем для каждого параметра
let lastChangeTime = {
    is_powered: 0,
    is_auto_mode: 0,
    brightness: 0
};

const SYNC_LOCK_MS = 6000; // Блокировка синхронизации на 6 сек (возвращено по просьбе пользователя)

// Элементы управления
const btnPowerOff = document.getElementById('btn-power-off');
const btnPowerOn = document.getElementById('btn-power-on');
const btnModeManual = document.getElementById('btn-mode-manual');
const btnModeAuto = document.getElementById('btn-mode-auto');
const btnBrightDown = document.getElementById('btn-bright-down');
const btnBrightUp = document.getElementById('btn-bright-up');
const brightnessValue = document.getElementById('brightness-value');
const brightnessSection = document.getElementById('brightness-section');

// Тест-кнопки
const btnTestDark = document.getElementById('test-dark');
const btnTestLight = document.getElementById('test-light');

// Телеметрия
const lightLevelEl = document.getElementById('light-level');
const pwmValueEl = document.getElementById('pwm-value');
const eventCountEl = document.getElementById('event-count');
const statusDot = document.getElementById('status-dot');

// ==========================================
// 1. Логика API
// ==========================================

async function sendCommand(params = {}) {
    const data = {
        device_id: DEVICE_ID,
        is_auto_mode: state.is_auto_mode,
        brightness: state.brightness,
        is_powered: state.is_powered,
        ...params
    };

    try {
        await fetch(`${API_URL}/command`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
    } catch (err) {
        console.error('Ошибка отправки команды:', err);
    }
}

async function fetchLatestData() {
    try {
        const response = await fetch(`${API_URL}/data?device_id=${DEVICE_ID}&limit=1`);
        const data = await response.json();
        
        if (data && data.length > 0) {
            updateUI(data[0]);
        }
    } catch (err) {
        console.error('Ошибка получения данных:', err);
        statusDot.classList.remove('status-online');
    }
}

// ==========================================
// 2. Обновление интерфейса
// ==========================================

function updateUI(data) {
    // 1. Телеметрия (датчики) обновляется ВСЕГДА
    lightLevelEl.innerText = data.light_level;
    pwmValueEl.innerText = data.pwm_value;
    eventCountEl.innerText = data.event_count;
    statusDot.classList.add('status-online');

    const now = Date.now();

    // 2. Синхронизация контролов (только если прошло SYNC_LOCK_MS с последнего нажатия)
    
    // Питание
    if (now - lastChangeTime.is_powered > SYNC_LOCK_MS) {
        state.is_powered = data.is_powered;
        updatePowerButtons();
    }

    // Режим
    if (now - lastChangeTime.is_auto_mode > SYNC_LOCK_MS) {
        state.is_auto_mode = data.is_auto_mode;
        updateModeButtons();
    }

    // Яркость
    if (now - lastChangeTime.brightness > SYNC_LOCK_MS) {
        state.brightness = data.brightness;
        brightnessValue.innerText = state.brightness;
        brightnessSection.classList.toggle('disabled', state.is_auto_mode);
    }
}

// ==========================================
// 3. Обработчики событий
// ==========================================

function updatePowerButtons() {
    btnPowerOn.classList.toggle('active', state.is_powered);
    btnPowerOff.classList.toggle('active', !state.is_powered);
}

function updateModeButtons() {
    btnModeAuto.classList.toggle('active', state.is_auto_mode);
    btnModeManual.classList.toggle('active', !state.is_auto_mode);
    brightnessSection.classList.toggle('disabled', state.is_auto_mode);
}

// Питание
btnPowerOn.addEventListener('click', () => { 
    state.is_powered = true; 
    lastChangeTime.is_powered = Date.now();
    updatePowerButtons(); 
    sendCommand(); 
});
btnPowerOff.addEventListener('click', () => { 
    state.is_powered = false; 
    lastChangeTime.is_powered = Date.now();
    updatePowerButtons(); 
    sendCommand(); 
});

// Режим
btnModeAuto.addEventListener('click', () => { 
    state.is_auto_mode = true; 
    lastChangeTime.is_auto_mode = Date.now();
    updateModeButtons(); 
    sendCommand(); 
});
btnModeManual.addEventListener('click', () => { 
    state.is_auto_mode = false; 
    lastChangeTime.is_auto_mode = Date.now();
    updateModeButtons(); 
    sendCommand(); 
});

// Яркость
btnBrightUp.addEventListener('click', () => {
    if (state.brightness < 10) {
        state.brightness++;
        lastChangeTime.brightness = Date.now();
        brightnessValue.innerText = state.brightness;
        sendCommand();
    }
});
btnBrightDown.addEventListener('click', () => {
    if (state.brightness > 0) {
        state.brightness--;
        lastChangeTime.brightness = Date.now();
        brightnessValue.innerText = state.brightness;
        sendCommand();
    }
});

// Тесты
btnTestDark.addEventListener('click', () => {
    state.is_auto_mode = true;
    lastChangeTime.is_auto_mode = Date.now();
    updateModeButtons();
    sendCommand({ threshold: 4095 });
    alert("Имитация темноты отправлена. В режиме AUTO лампа должна включиться.");
});

btnTestLight.addEventListener('click', () => {
    state.is_auto_mode = true;
    lastChangeTime.is_auto_mode = Date.now();
    updateModeButtons();
    sendCommand({ threshold: 0 });
    alert("Имитация света отправлена. В режиме AUTO лампа должна выключиться.");
});

// Запуск
setInterval(fetchLatestData, 2000);
fetchLatestData();
