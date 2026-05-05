const DEVICE_ID = 'b1a0fc95-8236-43be-af6a-cb315d05d75f';
const API_URL = 'https://web-lamp.vercel.app/api';

// Состояние
let currentBrightness = 5;
let isAutoMode = true;
let isPowered = false;
let lastInteractionTime = 0;
const COOLDOWN_MS = 6000;

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

function markInteraction() {
    lastInteractionTime = Date.now();
}

// ==========================================
// 1. Логика API
// ==========================================

async function sendCommand(params = {}) {
    markInteraction();
    
    const data = {
        device_id: DEVICE_ID,
        is_auto_mode: isAutoMode,
        brightness: currentBrightness,
        is_powered: isPowered,
        ...params // Можно переопределить любые поля (например, threshold)
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
    // Телеметрия обновляется всегда
    lightLevelEl.innerText = data.light_level;
    pwmValueEl.innerText = data.pwm_value;
    eventCountEl.innerText = data.event_count;
    statusDot.classList.add('status-online');

    // Контролы обновляются только если нет недавнего взаимодействия
    if (Date.now() - lastInteractionTime < COOLDOWN_MS) return;

    isPowered = data.is_powered;
    isAutoMode = data.is_auto_mode;
    currentBrightness = data.brightness;

    // Кнопки Питания
    btnPowerOn.classList.toggle('active', isPowered);
    btnPowerOff.classList.toggle('active', !isPowered);

    // Кнопки Режима
    btnModeAuto.classList.toggle('active', isAutoMode);
    btnModeManual.classList.toggle('active', !isAutoMode);

    // Яркость
    brightnessValue.innerText = currentBrightness;
    brightnessSection.classList.toggle('disabled', isAutoMode);
}

// ==========================================
// 3. Обработчики событий
// ==========================================

// Питание
btnPowerOn.addEventListener('click', () => { isPowered = true; updateButtons(); sendCommand(); });
btnPowerOff.addEventListener('click', () => { isPowered = false; updateButtons(); sendCommand(); });

// Режим
btnModeAuto.addEventListener('click', () => { isAutoMode = true; updateButtons(); sendCommand(); });
btnModeManual.addEventListener('click', () => { isAutoMode = false; updateButtons(); sendCommand(); });

// Яркость
btnBrightUp.addEventListener('click', () => {
    if (currentBrightness < 10) {
        currentBrightness++;
        brightnessValue.innerText = currentBrightness;
        sendCommand();
    }
});
btnBrightDown.addEventListener('click', () => {
    if (currentBrightness > 0) {
        currentBrightness--;
        brightnessValue.innerText = currentBrightness;
        sendCommand();
    }
});

// Тесты
btnTestDark.addEventListener('click', () => {
    // Чтобы тест сработал, лампа должна быть в AUTO
    isAutoMode = true;
    updateButtons();
    sendCommand({ threshold: 4095 }); // Максимальный порог = всегда темно
    alert("Имитация темноты отправлена. В режиме AUTO лампа должна включиться.");
});

btnTestLight.addEventListener('click', () => {
    isAutoMode = true;
    updateButtons();
    sendCommand({ threshold: 0 }); // Минимальный порог = всегда светло
    alert("Имитация света отправлена. В режиме AUTO лампа должна выключиться.");
});

function updateButtons() {
    btnPowerOn.classList.toggle('active', isPowered);
    btnPowerOff.classList.toggle('active', !isPowered);
    btnModeAuto.classList.toggle('active', isAutoMode);
    btnModeManual.classList.toggle('active', !isAutoMode);
    brightnessSection.classList.toggle('disabled', isAutoMode);
}

// Запуск
setInterval(fetchLatestData, 2000);
fetchLatestData();
