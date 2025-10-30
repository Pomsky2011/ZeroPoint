#include "emulatorwidget.h"
#include <QPainter>
#include <QTimerEvent>

EmulatorWidget::EmulatorWidget(QWidget *parent)
    : QWidget(parent)
    , currentROM(nullptr)
    , frameBuffer(ZeroPoint::FB_WIDTH, ZeroPoint::FULL_HEIGHT, QImage::Format_RGB32)
    , timerId(-1)
    , running(false)
{
    setFixedSize(ZeroPoint::FB_WIDTH * 2, ZeroPoint::FULL_HEIGHT * 2);

    // Initialize framebuffer with test pattern
    for (int y = 0; y < ZeroPoint::FULL_HEIGHT; y++) {
        for (int x = 0; x < ZeroPoint::FB_WIDTH; x++) {
            uint16_t red = (x * 31 / 255) << 1;
            uint16_t blue = (y * 31 / 255) << 11;
            display.setPixel(x, y, red | blue);
        }
    }

    updateFrameBuffer();
}

EmulatorWidget::~EmulatorWidget()
{
    if (timerId != -1) {
        killTimer(timerId);
    }
}

void EmulatorWidget::loadROM(ZeroPoint::ROM *rom)
{
    currentROM = rom;
    reset();
}

void EmulatorWidget::start()
{
    if (!running && currentROM && currentROM->isLoaded()) {
        running = true;
        // Start timer for ~60 FPS
        timerId = startTimer(16); // ~60 Hz
    }
}

void EmulatorWidget::stop()
{
    if (running) {
        running = false;
        if (timerId != -1) {
            killTimer(timerId);
            timerId = -1;
        }
    }
}

void EmulatorWidget::reset()
{
    // Reset display
    // For now, just keep the test pattern
    updateFrameBuffer();
    update();
}

void EmulatorWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.drawImage(rect(), frameBuffer);
}

void EmulatorWidget::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == timerId && running) {
        // Run one frame (261 scanlines * 340 pixels)
        for (int i = 0; i < ZeroPoint::TOTAL_SCANLINES * ZeroPoint::TOTAL_PIXELS_PER_LINE; i++) {
            display.tick();
        }

        updateFrameBuffer();
        update();
    }
}

void EmulatorWidget::updateFrameBuffer()
{
    // Use getPixel() to read from rolling buffer
    // Pixels outside the 8-scanline window will be black
    for (int y = 0; y < ZeroPoint::FULL_HEIGHT; y++) {
        QRgb *scanline = reinterpret_cast<QRgb*>(frameBuffer.scanLine(y));
        for (int x = 0; x < ZeroPoint::FB_WIDTH; x++) {
            ZeroPoint::Color32 color = display.getPixel(x, y);
            // RGBA32 format: RRGGBBAA
            uint8_t r = (color >> 24) & 0xFF;
            uint8_t g = (color >> 16) & 0xFF;
            uint8_t b = (color >> 8) & 0xFF;
            // Ignore alpha for now
            scanline[x] = qRgb(r, g, b);
        }
    }
}

QRgb EmulatorWidget::convertColor(ZeroPoint::Color16 color)
{
    // Format: BBBBBGGGGGRRRRR-
    // Extract 5-bit components (ignore bit 0)
    uint8_t r5 = (color >> 1) & 0x1F;
    uint8_t g5 = (color >> 6) & 0x1F;
    uint8_t b5 = (color >> 11) & 0x1F;

    // Convert 5-bit to 8-bit by scaling
    uint8_t r8 = (r5 * 255) / 31;
    uint8_t g8 = (g5 * 255) / 31;
    uint8_t b8 = (b5 * 255) / 31;

    return qRgb(r8, g8, b8);
}
