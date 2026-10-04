#include "updates/UpdateSelfTest.h"
#include "updates/UpdateManifest.h"

#include <QByteArray>
#include <QString>

#ifdef Q_OS_MACOS
#include "core/AutomatedRun.h"
#include "platform/mac/UpdateHelperMac.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#endif

namespace {

using Check = std::function<void(bool ok, const QString &what)>;

const char kDigestA[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char kDigestB[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

QByteArray manifest(const QByteArray &product)
{
    return "{\"schemaVersion\":1,\"products\":{\"legandrop/LGA_MightyTools\":" + product + "}}";
}

QByteArray asset(const char *name, const char *digest, const char *tag = nullptr)
{
    QByteArray json = "{";
    if (tag) {
        json += "\"tag\":\"" + QByteArray(tag) + "\",";
    }
    json += "\"name\":\"" + QByteArray(name) + "\",\"digest\":\"" + QByteArray(digest) + "\",\"size\":1}";
    return json;
}

void testManifest(const Check &check)
{
    using UpdateManifest::Platform;
    const QByteArray sha256A = QByteArray("sha256:") + kDigestA;
    const QByteArray sha256B = QByteArray("sha256:") + kDigestB;

    // Cada plataforma en su release: Windows ya esta en 1.31 y el ultimo ZIP de mac es el de 1.30.
    const QByteArray split = manifest(
        "{\"tag\":\"v1.31\",\"assets\":[" + asset("LGA_MightyTools_Setup_v1.31.exe", sha256A.constData())
        + "],\"assetLatest\":[" + asset("LGA_MightyTools_Mac_v1.30.dmg", sha256B.constData(), "v1.30") + ","
        + asset("LGA_MightyTools_Mac_v1.30.zip", sha256B.constData(), "v1.30") + ","
        + asset("LGA_MightyTools_Setup_v1.31.exe", sha256A.constData(), "v1.31") + ","
        + asset("SHA256SUMS", sha256A.constData(), "v1.31") + "]}");
    const UpdateManifest::ReleaseInfo win = UpdateManifest::parse(split, Platform::Windows);
    check(win.version == QLatin1String("1.31") && win.tag == QLatin1String("v1.31")
              && win.assetName == QLatin1String("LGA_MightyTools_Setup_v1.31.exe")
              && win.assetDigest == QLatin1String(kDigestA),
          QStringLiteral("manifiesto: Windows toma su instalador de assetLatest"));
    const UpdateManifest::ReleaseInfo mac = UpdateManifest::parse(split, Platform::Mac);
    check(mac.version == QLatin1String("1.30") && mac.tag == QLatin1String("v1.30")
              && mac.assetName == QLatin1String("LGA_MightyTools_Mac_v1.30.zip")
              && mac.assetDigest == QLatin1String(kDigestB),
          QStringLiteral("manifiesto: mac toma el ZIP de su propio release, no el DMG ni el tag de Windows"));

    // Publicado solo para Windows: en mac no hay nada que ofrecer.
    const QByteArray onlyWindows = manifest(
        "{\"tag\":\"v1.21\",\"assets\":[" + asset("LGA_MightyTools_Setup_v1.21.exe", sha256A.constData())
        + "],\"assetLatest\":[" + asset("LGA_MightyTools_Setup_v1.21.exe", sha256A.constData(), "v1.21") + "]}");
    check(UpdateManifest::parse(onlyWindows, Platform::Mac).version.isEmpty(),
          QStringLiteral("manifiesto: sin paquete de mac no hay update en mac"));
    check(UpdateManifest::parse(onlyWindows, Platform::Windows).version == QLatin1String("1.21"),
          QStringLiteral("manifiesto: ese mismo manifiesto si ofrece la 1.21 en Windows"));

    // Manifiesto viejo, sin assetLatest: tag + assets, como antes.
    const QByteArray legacy = manifest(
        "{\"tag\":\"v1.21\",\"assets\":[" + asset("LGA_MightyTools_Setup_v1.21.exe", sha256A.constData()) + "]}");
    const UpdateManifest::ReleaseInfo legacyWin = UpdateManifest::parse(legacy, Platform::Windows);
    check(legacyWin.version == QLatin1String("1.21") && legacyWin.assetDigest == QLatin1String(kDigestA),
          QStringLiteral("manifiesto: sin assetLatest se lee tag + assets"));
    const UpdateManifest::ReleaseInfo legacyMac = UpdateManifest::parse(legacy, Platform::Mac);
    check(legacyMac.version == QLatin1String("1.21") && legacyMac.assetName.isEmpty(),
          QStringLiteral("manifiesto: sin assetLatest y sin ZIP, la version no trae paquete de mac"));

    // Dos entradas de la misma familia: gana la version mas alta (1.10 > 1.9).
    const QByteArray twoZips = manifest(
        "{\"tag\":\"v1.10\",\"assets\":[],\"assetLatest\":["
        + asset("LGA_MightyTools_Mac_v1.10.zip", sha256A.constData(), "v1.10") + ","
        + asset("LGA_MightyTools_Mac_v1.9.zip", sha256B.constData(), "v1.9") + "]}");
    check(UpdateManifest::parse(twoZips, Platform::Mac).version == QLatin1String("1.10"),
          QStringLiteral("manifiesto: entre dos ZIP gana la version mas alta"));

    // Sin digest valido el paquete se informa sin digest (UpdateService se niega a bajarlo).
    const QByteArray noDigest = manifest(
        "{\"tag\":\"v1.30\",\"assets\":[],\"assetLatest\":["
        + asset("LGA_MightyTools_Mac_v1.30.zip", "sha256:1234", "v1.30") + "]}");
    const UpdateManifest::ReleaseInfo withoutDigest = UpdateManifest::parse(noDigest, Platform::Mac);
    check(withoutDigest.version == QLatin1String("1.30") && withoutDigest.assetDigest.isEmpty(),
          QStringLiteral("manifiesto: un digest invalido queda vacio"));

    // Nombres parecidos que no son el paquete.
    const QByteArray lookalikes = manifest(
        "{\"tag\":\"v1.30\",\"assets\":[],\"assetLatest\":["
        + asset("LGA_MightyTools_Mac_v1.30.zip.sig", sha256A.constData(), "v1.30") + ","
        + asset("LGA_MightyTools_Mac_vbeta.zip", sha256A.constData(), "v1.30") + ","
        + asset("Otro_LGA_MightyTools_Mac_v1.30.zip", sha256A.constData(), "v1.30") + "]}");
    check(UpdateManifest::parse(lookalikes, Platform::Mac).version.isEmpty(),
          QStringLiteral("manifiesto: los nombres parecidos no cuentan como paquete de mac"));

    // Los flags de prueba del updater solo pueden apuntar a esta maquina.
    const auto loopback = [](const char *text) { return UpdateManifest::loopbackUrl(QString::fromLatin1(text)).isValid(); };
    check(loopback("http://127.0.0.1:8765/versions.json") && loopback("http://localhost:8765/"),
          QStringLiteral("flags de prueba: 127.0.0.1 y localhost valen"));
    check(!loopback("https://127.0.0.1/versions.json") && !loopback("http://example.com/versions.json")
              && !loopback("http://127.0.0.1@example.com/versions.json") && !loopback("http://127.0.0.1.example.com/")
              && !loopback("file:///tmp/versions.json") && !loopback(""),
          QStringLiteral("flags de prueba: otro host, otro esquema o un host disfrazado no valen"));

    check(UpdateManifest::parse("{\"schemaVersion\":2,\"products\":{}}", Platform::Mac).version.isEmpty()
              && UpdateManifest::parse("no es json", Platform::Windows).version.isEmpty()
              && UpdateManifest::parse("{\"schemaVersion\":1,\"products\":{}}", Platform::Windows).version.isEmpty(),
          QStringLiteral("manifiesto: otro schema, basura o repo ausente no dan update"));
}

#ifdef Q_OS_MACOS

// Identificador de los bundles de mentira: nunca el de la app real.
const QString kFakeId = QStringLiteral("com.lga.mightytools.selftest");
const QString kFakeName = QStringLiteral("Fake Tool.app");

int runProgram(const QString &program, const QStringList &arguments, const QProcessEnvironment &environment,
               int timeoutMs = 60000)
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(5000);
        return -1;
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

int runProgram(const QString &program, const QStringList &arguments)
{
    return runProgram(program, arguments, QProcessEnvironment::systemEnvironment());
}

bool writeText(const QString &path, const QByteArray &text, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size()) {
        return false;
    }
    file.close();
    if (executable) {
        return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                   | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                   | QFileDevice::ExeOther);
    }
    return true;
}

// Un bundle minimo, firmado ad-hoc. tamper: toca el ejecutable DESPUES de firmar (firma rota).
bool makeBundle(const QString &dir, const QString &bundleId, const QString &version, bool tamper = false)
{
    const QString app = QDir(dir).filePath(kFakeName);
    const QByteArray plist =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "<key>CFBundleIdentifier</key><string>" + bundleId.toUtf8() + "</string>\n"
        "<key>CFBundleShortVersionString</key><string>" + version.toUtf8() + "</string>\n"
        "<key>CFBundleExecutable</key><string>faketool</string>\n"
        "<key>CFBundlePackageType</key><string>APPL</string>\n"
        "</dict></plist>\n";
    if (!writeText(app + QStringLiteral("/Contents/Info.plist"), plist)
        || !writeText(app + QStringLiteral("/Contents/MacOS/faketool"), "#!/bin/sh\nexit 0\n", true)
        || !writeText(app + QStringLiteral("/Contents/Resources/version.txt"), version.toUtf8())) {
        return false;
    }
    if (runProgram(QStringLiteral("/usr/bin/codesign"), {QStringLiteral("--force"), QStringLiteral("--sign"),
                                                         QStringLiteral("-"), app}) != 0) {
        return false;
    }
    if (tamper) {
        return writeText(app + QStringLiteral("/Contents/Resources/version.txt"), "tocado despues de firmar");
    }
    return true;
}

bool zipBundle(const QString &dir, const QString &zipPath)
{
    QDir().mkpath(QFileInfo(zipPath).absolutePath());
    return runProgram(QStringLiteral("/usr/bin/ditto"),
                      {QStringLiteral("-c"), QStringLiteral("-k"), QStringLiteral("--sequesterRsrc"),
                       QStringLiteral("--keepParent"), QDir(dir).filePath(kFakeName), zipPath})
           == 0;
}

// Huella de todo lo que hay bajo `dir` (nombres y contenido, ocultos incluidos).
QByteArray fingerprint(const QString &dir)
{
    QStringList files;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(it.next());
    }
    files.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString &path : files) {
        hash.addData(QDir(dir).relativeFilePath(path).toUtf8());
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            hash.addData(file.readAll());
        }
    }
    return hash.result();
}

QString installedVersion(const QString &dir)
{
    QFile file(QDir(dir).filePath(kFakeName) + QStringLiteral("/Contents/Resources/version.txt"));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// Solo queda el bundle: ni carpeta de trabajo ni copia vieja.
bool onlyTheBundle(const QString &dir)
{
    return QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
           == QStringList{kFakeName};
}

void testHelper(const Check &check)
{
    QTemporaryDir sandboxDir;
    QTemporaryDir outsideDir;
    if (!sandboxDir.isValid() || !outsideDir.isValid()) {
        check(false, QStringLiteral("script de mac: no se pudo crear la carpeta de pruebas"));
        return;
    }
    const QString sandbox = QFileInfo(sandboxDir.path()).canonicalFilePath();
    const QString outside = QFileInfo(outsideDir.path()).canonicalFilePath();
    AutomatedRun::setSandbox(sandbox);
    struct SandboxReset {
        ~SandboxReset() { AutomatedRun::setSandbox(QString()); }
    } sandboxReset;

    const QString logs = sandbox + QStringLiteral("/logs");
    const QString zips = sandbox + QStringLiteral("/zips");
    QDir().mkpath(logs);
    QDir().mkpath(zips);

    // Los paquetes de mentira. Cada caso usa su copia: el script borra el ZIP que recibe.
    const QString good = sandbox + QStringLiteral("/src/good");
    const QString otherId = sandbox + QStringLiteral("/src/other-id");
    const QString broken = sandbox + QStringLiteral("/src/broken");
    const bool built = makeBundle(good, kFakeId, QStringLiteral("2.0"))
                       && makeBundle(otherId, QStringLiteral("com.example.otra"), QStringLiteral("2.0"))
                       && makeBundle(broken, kFakeId, QStringLiteral("2.0"), /*tamper=*/true)
                       && zipBundle(good, zips + QStringLiteral("/good.zip"))
                       && zipBundle(otherId, zips + QStringLiteral("/other-id.zip"))
                       && zipBundle(broken, zips + QStringLiteral("/broken.zip"))
                       && writeText(zips + QStringLiteral("/corrupt.zip"), QByteArray(4096, 'x'));
    check(built, QStringLiteral("script de mac: bundles y ZIP de mentira armados y firmados"));
    if (!built) {
        return;
    }

    int caseNumber = 0;
    // Un bundle "instalado" v1.0 en una carpeta nueva bajo `root`.
    const auto freshInstall = [&caseNumber](const QString &root) {
        const QString dir = root + QStringLiteral("/installed-%1").arg(++caseNumber);
        return makeBundle(dir, kFakeId, QStringLiteral("1.0")) ? dir : QString();
    };
    const auto caseZip = [&zips, &caseNumber](const QString &name) {
        const QString copy = zips + QStringLiteral("/case-%1-%2").arg(caseNumber).arg(name);
        QFile::copy(zips + QLatin1Char('/') + name, copy);
        return copy;
    };
    const auto jobFor = [&](const QString &installDir, const QString &zip) {
        MacUpdateHelper::Job job;
        job.zipPath = zip;
        job.targetBundle = QDir(installDir).filePath(kFakeName);
        job.bundleId = kFakeId;
        job.version = QStringLiteral("2.0");
        job.logPath = logs + QStringLiteral("/case-%1.log").arg(caseNumber);
        job.testMode = true;
        job.allowedRoot = sandbox;
        return job;
    };
    // Corre el script. guarded=false saltea la guarda de C++ (solo el canario lo usa).
    const auto runJob = [&](const MacUpdateHelper::Job &job, bool guarded = true, int waitSeconds = 0) {
        if (guarded && !MacUpdateHelper::allowed(job)) {
            return -2;
        }
        const QString script = MacUpdateHelper::writeScript(zips);
        if (script.isEmpty()) {
            return -3;
        }
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        if (waitSeconds > 0) {
            environment.insert(QStringLiteral("LGA_UPDATE_WAIT_SECONDS"), QString::number(waitSeconds));
        }
        const int code = runProgram(QStringLiteral("/bin/bash"), MacUpdateHelper::arguments(script, job), environment);
        QFile::remove(script);
        return code;
    };

    // --- Canario: un destino FUERA de la carpeta de pruebas tiene que ser rechazado por las dos
    // guardas, sin tocar nada. Si no, no se corre ningun otro caso.
    {
        const QString install = freshInstall(outside);
        const QByteArray before = fingerprint(install);
        MacUpdateHelper::Job job = jobFor(install, caseZip(QStringLiteral("good.zip")));
        QString why;
        const bool refusedByCpp = !MacUpdateHelper::allowed(job, &why);
        const int code = runJob(job, /*guarded=*/false);
        const bool isolated = !install.isEmpty() && refusedByCpp && code == 4 && fingerprint(install) == before
                              && onlyTheBundle(install) && QFile::exists(job.zipPath);
        check(isolated, QStringLiteral("script de mac: un destino fuera de la carpeta de pruebas se rechaza "
                                       "sin tocar nada (codigo %1)")
                            .arg(code));
        if (!isolated) {
            check(false, QStringLiteral("script de mac: aislamiento NO comprobado, se cortan los demas casos"));
            return;
        }

        MacUpdateHelper::Job realId = jobFor(freshInstall(sandbox), caseZip(QStringLiteral("good.zip")));
        realId.bundleId = QStringLiteral("com.lga.mightytools");
        MacUpdateHelper::Job runMode = realId;
        runMode.bundleId = kFakeId;
        runMode.testMode = false;
        check(!MacUpdateHelper::allowed(realId) && !MacUpdateHelper::allowed(runMode),
              QStringLiteral("script de mac: en una corrida automatizada no se acepta el identificador real "
                             "ni el modo real"));
    }

    // --- Caso feliz.
    {
        const QString install = freshInstall(sandbox);
        const MacUpdateHelper::Job job = jobFor(install, caseZip(QStringLiteral("good.zip")));
        const int code = runJob(job);
        const bool verified = runProgram(QStringLiteral("/usr/bin/codesign"),
                                         {QStringLiteral("--verify"), QStringLiteral("--deep"),
                                          QStringLiteral("--strict"), job.targetBundle})
                              == 0;
        check(code == 0 && installedVersion(install) == QLatin1String("2.0") && onlyTheBundle(install) && verified
                  && !QFile::exists(job.zipPath),
              QStringLiteral("script de mac: reemplaza la 1.0 por la 2.0 y no deja restos (codigo %1)").arg(code));
    }

    // --- Fallas: el bundle instalado queda byte a byte igual y sin restos al lado.
    struct Failure {
        QString zip;
        QString version;
        int expected;
        QString what;
    };
    const QList<Failure> failures = {
        {QStringLiteral("corrupt.zip"), QStringLiteral("2.0"), 5, QStringLiteral("ZIP corrupto")},
        {QStringLiteral("other-id.zip"), QStringLiteral("2.0"), 6, QStringLiteral("otro identificador")},
        {QStringLiteral("good.zip"), QStringLiteral("3.0"), 6, QStringLiteral("otra version")},
        {QStringLiteral("broken.zip"), QStringLiteral("2.0"), 7, QStringLiteral("firma rota")},
    };
    for (const Failure &failure : failures) {
        const QString install = freshInstall(sandbox);
        const QByteArray before = fingerprint(install);
        MacUpdateHelper::Job job = jobFor(install, caseZip(failure.zip));
        job.version = failure.version;
        const int code = runJob(job);
        check(code == failure.expected && fingerprint(install) == before && onlyTheBundle(install),
              QStringLiteral("script de mac: %1 -> no se instala y la instalada queda igual (codigo %2)")
                  .arg(failure.what)
                  .arg(code));
    }

    // --- La app no cierra: se espera (acortado a 1 s) y se deja todo como estaba, sin matar a nadie.
    {
        const QString install = freshInstall(sandbox);
        const QByteArray before = fingerprint(install);
        MacUpdateHelper::Job job = jobFor(install, caseZip(QStringLiteral("good.zip")));
        job.waitPid = QCoreApplication::applicationPid();
        const int code = runJob(job, /*guarded=*/true, /*waitSeconds=*/1);
        check(code == 3 && fingerprint(install) == before && onlyTheBundle(install),
              QStringLiteral("script de mac: si la app no cierra no se reemplaza (codigo %1)").arg(code));
    }

    // --- Carpeta de instalacion sin permiso de escritura.
    {
        const QString install = freshInstall(sandbox);
        const QByteArray before = fingerprint(install);
        const MacUpdateHelper::Job job = jobFor(install, caseZip(QStringLiteral("good.zip")));
        QFile::setPermissions(install, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const int code = runJob(job);
        QFile::setPermissions(install, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        check(code == 8 && fingerprint(install) == before && onlyTheBundle(install),
              QStringLiteral("script de mac: carpeta sin permiso de escritura -> no se instala (codigo %1)").arg(code));
    }
}

#endif // Q_OS_MACOS

} // namespace

namespace UpdateSelfTest {

void run(const std::function<void(bool ok, const QString &what)> &check)
{
    testManifest(check);
#ifdef Q_OS_MACOS
    // Unica excepcion del updater a "en una corrida automatizada nunca se ejecuta una accion real"
    // (mismo criterio que el self-test de la limpieza de discos): el script corre de verdad, pero en
    // modo test, sobre bundles de mentira con otro identificador, dentro de una carpeta que este
    // self-test crea y registra con AutomatedRun::setSandbox(). El primer caso prueba el aislamiento.
    testHelper(check);
#endif
}

} // namespace UpdateSelfTest
