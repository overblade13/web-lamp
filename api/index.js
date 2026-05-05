const express = require('express');
const cors = require('cors');
const { createClient } = require('@supabase/supabase-js');
const path = require('path');
require('dotenv').config();

const app = express();

// Middleware
app.use(cors()); // Разрешаем запросы с других доменов (от фронтенда)
app.use(express.json()); // Парсим тело запроса как JSON

// Раздача статических файлов (фронтенд)
app.use(express.static(path.join(__dirname, '../public')));

// Инициализация Supabase клиента
const supabaseUrl = process.env.SUPABASE_URL || '';
const supabaseKey = process.env.SUPABASE_ANON_KEY || '';
const supabase = createClient(supabaseUrl, supabaseKey);

// ==========================================
// 1. Эндпоинт для ESP32: Отправка телеметрии и получение настроек
// ==========================================
app.post('/api/telemetry', async (req, res) => {
  try {
    let { 
      device_id, 
      light_level, 
      brightness, 
      is_powered, 
      is_auto_mode, 
      pwm_value, 
      event_count 
    } = req.body;

    if (!device_id) {
      return res.status(400).json({ error: 'device_id обязателен' });
    }

    // Жесткая серверная валидация: яркость строго от 0 до 10
    if (brightness !== undefined) {
      brightness = Math.max(0, Math.min(10, Number(brightness)));
    }

    // 1. Сохраняем показания в БД (таблица sensor_readings)
    const { error: insertError } = await supabase
      .from('sensor_readings')
      .insert([{
        device_id,
        light_level,
        brightness,
        is_powered,
        is_auto_mode,
        pwm_value,
        event_count
      }]);

    if (insertError) {
      console.error('Ошибка вставки данных:', insertError);
      return res.status(500).json({ error: 'Ошибка сохранения данных' });
    }

    // Опционально: обновляем статус устройства (онлайн и время последней активности)
    await supabase
      .from('devices')
      .update({ is_online: true, last_seen: new Date().toISOString() })
      .eq('id', device_id);

    // Если настройки были изменены кнопками на самом устройстве, 
    // обновляем таблицу settings, чтобы фронтенд увидел изменения
    if (req.body.local_settings_changed) {
      const { error: updateError } = await supabase
        .from('settings')
        .upsert({
          device_id,
          brightness,
          is_auto_mode,
          is_powered,
          updated_at: new Date().toISOString()
        }, { onConflict: 'device_id' });

      if (updateError) {
        console.error('Ошибка синхронизации локальных настроек:', updateError);
      }
    }

    // 2. Получаем текущие настройки для этого устройства из таблицы settings
    const { data: settings, error: selectError } = await supabase
      .from('settings')
      .select('brightness, is_auto_mode, threshold, is_powered')
      .eq('device_id', device_id)
      .single();

    if (selectError) {
      if (selectError.code === 'PGRST116') {
        // Строки нет, автоматически создаем дефолтную запись для этого устройства
        const defaultSettings = {
          device_id,
          brightness: req.body.brightness !== undefined ? req.body.brightness : 5,
          is_auto_mode: req.body.is_auto_mode !== undefined ? req.body.is_auto_mode : true,
          is_powered: req.body.is_powered !== undefined ? req.body.is_powered : false,
          threshold: 500,
          updated_at: new Date().toISOString()
        };
        await supabase.from('settings').insert([defaultSettings]);
        
        return res.status(200).json(defaultSettings);
      } else {
        console.error('Ошибка получения настроек:', selectError);
        return res.status(200).json({
          brightness: req.body.brightness !== undefined ? req.body.brightness : 5,
          is_auto_mode: req.body.is_auto_mode !== undefined ? req.body.is_auto_mode : true,
          is_powered: req.body.is_powered !== undefined ? req.body.is_powered : false,
          threshold: 500
        });
      }
    }

    // 3. Отправляем настройки в ответ ESP32
    res.status(200).json(settings);
  } catch (error) {
    console.error('Внутренняя ошибка в /api/telemetry:', error);
    res.status(500).json({ error: 'Внутренняя ошибка сервера' });
  }
});

// ==========================================
// 2. Эндпоинт для фронтенда: Получение данных датчиков
// ==========================================
app.get('/api/data', async (req, res) => {
  try {
    const { device_id, limit = 20 } = req.query;
    
    // Формируем запрос к Supabase для получения последних записей
    let query = supabase
      .from('sensor_readings')
      .select('*')
      .order('created_at', { ascending: false })
      .limit(parseInt(limit));
      
    // Если передан ID конкретного устройства, фильтруем по нему
    if (device_id) {
      query = query.eq('device_id', device_id);
    }

    const { data, error } = await query;

    if (error) {
      console.error('Ошибка получения данных:', error);
      return res.status(500).json({ error: 'Ошибка получения данных из БД' });
    }

    res.status(200).json(data);
  } catch (error) {
    console.error('Внутренняя ошибка в /api/data:', error);
    res.status(500).json({ error: 'Внутренняя ошибка сервера' });
  }
});

// ==========================================
// 3. Эндпоинт для фронтенда: Отправка команд
// ==========================================
app.post('/api/command', async (req, res) => {
  try {
    const { device_id, brightness, is_auto_mode, threshold, is_powered } = req.body;

    if (!device_id) {
      return res.status(400).json({ error: 'device_id обязателен' });
    }

    // Подготавливаем объект только с теми полями, которые переданы в запросе
    const updateData = { updated_at: new Date().toISOString() };
    if (brightness !== undefined) {
      updateData.brightness = Math.max(0, Math.min(10, Number(brightness)));
    }
    if (is_auto_mode !== undefined) updateData.is_auto_mode = is_auto_mode;
    if (is_powered !== undefined) updateData.is_powered = is_powered;
    if (threshold !== undefined) updateData.threshold = threshold;

    // Обновляем настройки в БД
    const { data, error } = await supabase
      .from('settings')
      .update(updateData)
      .eq('device_id', device_id)
      .select();

    if (error) {
      console.error('Ошибка обновления настроек:', error);
      return res.status(500).json({ error: 'Ошибка обновления настроек в БД' });
    }

    res.status(200).json({ success: true, message: 'Настройки обновлены', data });
  } catch (error) {
    console.error('Внутренняя ошибка в /api/command:', error);
    res.status(500).json({ error: 'Внутренняя ошибка сервера' });
  }
});

// Проверка работы сервера (Healthcheck)
app.get('/api/health', (req, res) => {
  res.status(200).json({ status: 'ok', timestamp: new Date().toISOString() });
});

// Запуск сервера для локальной разработки
if (require.main === module) {
  const PORT = process.env.PORT || 3000;
  app.listen(PORT, () => {
    console.log(`Сервер запущен на порту ${PORT}`);
  });
}

// Экспорт приложения для Vercel Serverless Functions
module.exports = app;
