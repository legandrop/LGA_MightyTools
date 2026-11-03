#include "modules/dictation/DictationQa.h"

#include "app/HotkeyHub.h"
#include "core/AutomatedRun.h"
#include "modules/dictation/DictationTrigger.h"
#include "platform/AudioCapture.h"
#include "platform/TextTyper.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVector>
#include <QtMath>

#include <cmath>
#include <cstdio>

namespace DictationQa {

namespace {

using Check = std::function<void(bool, const QString &)>;
using Action = DictationTrigger::Action;

void testTrigger(const Check &check)
{
    DictationTrigger trigger;
    check(trigger.press(1000) == Action::Start && trigger.recording(), QStringLiteral("atajo: apretar empieza a grabar"));
    check(trigger.release(1250) == Action::None && trigger.state() == DictationTrigger::State::Latched,
          QStringLiteral("atajo: soltar antes de 300 ms deja grabando (tocar)"));
    check(trigger.press(5000) == Action::Finish && !trigger.recording() && trigger.release(5080) == Action::None,
          QStringLiteral("atajo: el proximo toque termina y su liberacion no hace nada"));
    check(trigger.press(100) == Action::Start && trigger.release(400) == Action::Finish && !trigger.recording(),
          QStringLiteral("atajo: mantener 300 ms o mas y soltar termina (mantener)"));
    check(trigger.press(0) == Action::Start && trigger.release(299) == Action::None && trigger.recording(),
          QStringLiteral("atajo: a los 299 ms todavia es un toque"));
    check(trigger.cancel() == Action::Cancel && !trigger.recording() && trigger.cancel() == Action::None,
          QStringLiteral("atajo: Esc cancela; sin grabar no hace nada"));
    trigger.press(0);
    check(trigger.cancel() == Action::Cancel && trigger.release(900) == Action::None,
          QStringLiteral("atajo: Esc mientras se mantiene cancela, y soltar despues no termina nada"));
    trigger.press(0);
    check(trigger.press(2000) == Action::Finish && !trigger.recording(),
          QStringLiteral("atajo: si la liberacion se perdio, volver a apretar termina"));
    trigger.setMaxDurationMs(30000);
    trigger.press(1000);
    check(trigger.tick(30999) == Action::None && trigger.tick(31000) == Action::Finish && !trigger.recording(),
          QStringLiteral("atajo: el tope de duracion termina solo (mantener)"));
    trigger.press(0);
    trigger.release(100);
    check(trigger.tick(29999) == Action::None && trigger.tick(30000) == Action::Finish,
          QStringLiteral("atajo: el tope vale tambien en modo tocar"));
    trigger.setMaxDurationMs(0);
    trigger.press(0);
    check(trigger.tick(10000000) == Action::None, QStringLiteral("atajo: sin tope no termina solo"));
    trigger.reset();
    check(!trigger.recording() && trigger.release(50) == Action::None,
          QStringLiteral("atajo: reset (fallo el microfono) vuelve a esperar sin accion"));
}

void testHotkeyRelease(const Check &check)
{
    // La liberacion viaja HotkeyService -> HotkeyHub -> la herramienta, por su id local (el hub corre en modo
    // automatizado: sin el servicio del sistema).
    HotkeyHub hub(true, {});
    ModuleHotkeysImpl *hotkeys = hub.acquire(QStringLiteral("dictation"), QStringLiteral("Dictation"), 4);
    QList<int> activated;
    QList<int> released;
    QObject::connect(hotkeys, &ModuleHotkeys::activated, [&activated](int id) { activated << id; });
    QObject::connect(hotkeys, &ModuleHotkeys::released, [&released](int id) { released << id; });
    const int globalId = 5 * ModuleHotkeysImpl::kRangeSize + 1;
    hotkeys->registerPriorityHotkey(1, Shortcut::fromPortableString(QStringLiteral("Meta+H")));
    hotkeys->fire(globalId);
    hotkeys->fire(globalId, true);
    check(activated == QList<int>{1} && released == QList<int>{1},
          QStringLiteral("atajo: la liberacion llega a la herramienta con su id local, aparte de la activacion"));
    hotkeys->unregisterHotkey(1);
    hotkeys->fire(globalId, true);
    check(released.size() == 1, QStringLiteral("atajo: un atajo retirado no avisa al soltar"));
    delete hotkeys;
}

void testAudio(const Check &check)
{
    {
        AudioCapture::Converter converter(AudioCapture::kSampleRate, 1);
        const float in[] = {0.1f, -0.2f, 0.3f};
        QVector<float> out;
        converter.push(in, 3, &out);
        check(out == QVector<float>{0.1f, -0.2f, 0.3f}, QStringLiteral("audio: 16 kHz mono pasa igual"));
    }
    {
        AudioCapture::Converter converter(48000, 2);
        QVector<float> in;
        for (int i = 0; i < 4800; ++i) in << 0.5f << -0.5f;
        QVector<float> out;
        converter.push(in.constData(), 4800, &out);
        bool silent = true;
        for (const float sample : out) silent = silent && qAbs(sample) < 1e-6f;
        check(out.size() == 1600 && silent,
              QStringLiteral("audio: estereo de 48 kHz a mono de 16 kHz: 100 ms dan 1600 muestras y L/R opuestos se anulan"));
    }
    {
        // Un seno de 440 Hz a 44,1 kHz estereo: de a bloques irregulares da lo mismo que de una vez.
        constexpr int kFrames = 4410;
        QVector<float> in;
        for (int i = 0; i < kFrames; ++i) {
            const float s = float(std::sin(2.0 * M_PI * 440.0 * i / 44100.0)) * 0.5f;
            in << s << s;
        }
        AudioCapture::Converter whole(44100, 2);
        AudioCapture::Converter pieces(44100, 2);
        QVector<float> a;
        QVector<float> b;
        whole.push(in.constData(), kFrames, &a);
        int done = 0;
        for (int size : {1, 7, 100, 333, 1000, 13}) {
            pieces.push(in.constData() + done * 2, size, &b);
            done += size;
        }
        pieces.push(in.constData() + done * 2, kFrames - done, &b);
        double worst = 0.0;
        for (int k = 0; k < a.size(); ++k) {
            worst = qMax(worst, qAbs(a.at(k) - 0.5 * std::sin(2.0 * M_PI * 440.0 * k / 16000.0)));
        }
        check(a == b && qAbs(a.size() - 1600) <= 1 && worst < 0.01,
              QStringLiteral("audio: 44,1 kHz por bloques da lo mismo que de una vez, sin saltos (error %1)").arg(worst, 0, 'g', 2));
    }
    check(AudioCapture::levelOf(QVector<float>(800, 0.0f)) == 0.0f && AudioCapture::levelOf({}) == 0.0f,
          QStringLiteral("nivel: silencio da 0"));
    check(qAbs(AudioCapture::levelOf(QVector<float>(800, 1.0f)) - 1.0f) < 1e-6f
              && qAbs(AudioCapture::levelOf(QVector<float>(800, 0.1f)) - 2.0f / 3.0f) < 1e-3f,
          QStringLiteral("nivel: escala completa da 1 y -20 dBFS da 2/3"));
}

void testTyping(const Check &check)
{
    const auto unicode = [](const char *hex) {
        return QStringList{QStringLiteral("unicode U+%1 abajo").arg(QLatin1String(hex)),
                           QStringLiteral("unicode U+%1 arriba").arg(QLatin1String(hex))};
    };
    const QStringList shiftEnter = {QStringLiteral("tecla vk=0x10 abajo"), QStringLiteral("tecla vk=0x0d abajo"),
                                    QStringLiteral("tecla vk=0x0d arriba"), QStringLiteral("tecla vk=0x10 arriba")};
    check(TextTyper::describe(QStringLiteral("á")) == unicode("00E1") && TextTyper::describe(QStringLiteral("ñ")) == unicode("00F1"),
          QStringLiteral("escritura: la a con tilde y la enie van como caracteres Unicode, sin depender del teclado"));
    const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
    check(TextTyper::describe(emoji) == unicode("D83D") + unicode("DE00"),
          QStringLiteral("escritura: un emoji va como su par sustituto, en orden"));
    check(TextTyper::describe(QStringLiteral("a\r\nb")) == unicode("0061") + shiftEnter + unicode("0062")
              && TextTyper::describe(QStringLiteral("\n")) == shiftEnter && TextTyper::describe(QStringLiteral("\r")) == shiftEnter,
          QStringLiteral("escritura: un salto de linea (\\r\\n, \\n o \\r) es UN Shift+Enter: en un chat Enter solo enviaria"));
    check(TextTyper::describe(QString(QChar(0x01))).isEmpty() && TextTyper::describe(QString()).isEmpty(),
          QStringLiteral("escritura: los caracteres de control no se mandan"));
    const QStringList atEnd = TextTyper::describe(QString(11, QLatin1Char('x')) + emoji + QLatin1Char('y'));
    const QStringList pushed = TextTyper::describe(QString(12, QLatin1Char('x')) + emoji);
    check(atEnd.indexOf(QStringLiteral("--")) == 26 && atEnd.mid(22, 4) == unicode("D83D") + unicode("DE00")
              && pushed.indexOf(QStringLiteral("--")) == 24 && pushed.mid(25) == unicode("D83D") + unicode("DE00"),
          QStringLiteral("escritura: lotes de 12 caracteres; un emoji nunca queda partido entre dos lotes"));
    const QStringList mixed = TextTyper::describe(QStringLiteral("Nuke, ¿andás?\n") + emoji);
    check(!mixed.isEmpty() && mixed.filter(QStringLiteral("SIN-MARCA")).isEmpty(),
          QStringLiteral("escritura: cada tecla lleva la marca propia (los ganchos de la app no la toman)"));
}

void testAutomatedRun(const Check &check)
{
    check(AutomatedRun::active(), QStringLiteral("corrida automatizada: el interruptor esta prendido"));
    AudioCapture capture;
    int events = 0;
    QObject::connect(&capture, &AudioCapture::started, [&events]() { ++events; });
    QObject::connect(&capture, &AudioCapture::failed, [&events]() { ++events; });
    check(!capture.start() && !capture.isRunning() && !capture.start(QStringLiteral("{0.0.1.00000000}.{falso}")),
          QStringLiteral("corrida automatizada: start() no abre el microfono ni arranca el hilo"));

    TextTyper typer;
    TextTyper::Result result = TextTyper::Result::Failed;
    int typed = -1;
    QEventLoop loop;
    QObject::connect(&typer, &TextTyper::finished, &loop, [&](TextTyper::Result r, int t) {
        result = r;
        typed = t;
        loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    // Una ventana inventada: en una corrida automatizada type() no la mira.
    const bool accepted = typer.type(quintptr(1), QStringLiteral("hola"));
    loop.exec();
    QCoreApplication::processEvents();
    check(accepted && result == TextTyper::Result::LoggedOnly && typed == 0 && !typer.isBusy() && events == 0,
          QStringLiteral("corrida automatizada: type() solo anota en el log; no manda ninguna tecla"));
}

} // namespace

void selfTest(const std::function<void(bool ok, const QString &what)> &check)
{
    testTrigger(check);
    testHotkeyRelease(check);
    testAudio(check);
    testTyping(check);
    testAutomatedRun(check);
}

int simulateAction(const QString &action, const QStringList & /*args*/)
{
    if (action != QLatin1String("devices")) {
        return 2;
    }
    const QList<AudioCapture::Device> devices = AudioCapture::devices();
    std::printf("dictation:devices %d microfonos activos (ninguno se abre)\n", int(devices.size()));
    for (const AudioCapture::Device &device : devices) {
        std::printf("  %s\t%s\n", device.name.toUtf8().constData(), device.id.toUtf8().constData());
    }
    return 0;
}

} // namespace DictationQa
