#include "updatecheck.h"
#include "process.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef ZACOS9_ARCH_PACKAGE
static const char DEFAULT_REPO[] = "gazecube/Archintosh-OS-9";
#else
static const char DEFAULT_REPO[] = "chromasystemprofessional/Zac-OS-9";
#endif

QString updateRepo() {
	const QString env = qEnvironmentVariable("ZACOS9_UPDATE_REPO");
	return env.isEmpty() ? QString::fromLatin1(DEFAULT_REPO) : env;
}

QString updateReleaseUrl() {
	const QString env = qEnvironmentVariable("ZACOS9_UPDATE_URL");
	return env.isEmpty() ? "https://api.github.com/repos/" + updateRepo() + "/releases/latest" : env;
}

Release parseRelease(const QByteArray &json, const QString &arch) {
	Release r;
	const QJsonObject o = QJsonDocument::fromJson(json).object();
	QString tag = o.value("tag_name").toString();
	if (tag.startsWith('v') || tag.startsWith('V')) {
		tag.remove(0, 1);
	}
	/* Keep release tags safe for the package manager and the window. */
	static const QRegularExpression versionRe("^[0-9][A-Za-z0-9.+~-]*$");
	if (!versionRe.match(tag).hasMatch()) {
		return r;
	}
	r.version = tag;
	r.name = o.value("name").toString();
	if (r.name.isEmpty()) {
		r.name = o.value("tag_name").toString();
	}
	r.notes = o.value("body").toString();
#ifdef ZACOS9_ARCH_PACKAGE
	const QString packagePrefix = "archintosh-os-9-" + r.version + "-";
	const QString suffix = "-1-" + arch + ".pkg.tar.zst";
#else
	const QString packagePrefix = "zacos9_";
	const QString suffix = "_" + arch + ".deb";
#endif
	for (const QJsonValue &v : o.value("assets").toArray()) {
		const QJsonObject a = v.toObject();
		const QString name = a.value("name").toString();
		if (!name.startsWith(packagePrefix) || !name.endsWith(suffix)) {
			continue;
		}
		const QString url = a.value("browser_download_url").toString();
		if (!url.startsWith("https://")) {
			continue;
		}
		r.debName = name;
		r.debUrl = url;
		const QString digest = a.value("digest").toString();
		if (digest.startsWith("sha256:")) {
			r.sha256 = digest.mid(7).toLower();
		}
		r.valid = true;
		break;
	}
	return r;
}

std::vector<DebianUpdate> parseAptSimulation(const QString &out) {
	/* "Inst libfoo1 [1.2-1] (1.2-2 Debian-Security:13/stable-security [amd64])"
	 * "Inst linux-image-6.12.9-amd64 (6.12.9-1 Debian:13.1/stable [amd64])" */
	static const QRegularExpression inst(R"(^Inst (\S+)(?: \[([^\]]+)\])? \((\S+))");
	std::vector<DebianUpdate> updates;
	for (const QString &line : out.split('\n')) {
		const auto m = inst.match(line);
		if (m.hasMatch()) {
			updates.push_back({ m.captured(1), m.captured(2), m.captured(3) });
		}
	}
	return updates;
}

/* A short local command (dpkg's own), run to the end while the event loop
 * keeps going: never QProcess::waitForFinished(), which has deadlocked in
 * this desktop's Qt/Wayland programs (see CLAUDE.md). */
static QString runQuiet(const QString &program, const QStringList &args, int *code = nullptr) {
	const ProcessResult r = runProcess(program, args);
	if (code) {
		*code = r.started && !r.crashed ? r.exitCode : -1;
	}
	return QString::fromUtf8(r.out).trimmed();
}

bool versionNewer(const QString &a, const QString &b) {
	if (a.isEmpty()) {
		return false;
	}
	if (b.isEmpty()) {
		return true;
	}
#ifdef ZACOS9_ARCH_PACKAGE
	int code = -1;
	const QString result = runQuiet("vercmp", { a, b }, &code);
	return code == 0 && result.toInt() > 0;
#else
	int code = -1;
	runQuiet("dpkg", { "--compare-versions", a, "gt", b }, &code);
	return code == 0;
#endif
}

QString debianCheckProblem(int refreshCode, int simulateCode, const QString &error) {
	if (simulateCode != 0) {
		return "Couldn’t look for Debian updates: " +
			(error.isEmpty() ? QString("apt didn’t finish") : error) + ".";
	}
	if (refreshCode != 0) {
		/* Not an administrator, offline, or a mirror down: the lists are
		 * the last ones fetched, and may miss updates since. */
		return "Debian’s list of updates couldn’t be fetched, so this is as of the last time it was.";
	}
	return QString();
}

QString installedVersion(const QString &package) {
#ifdef ZACOS9_ARCH_PACKAGE
	int code = -1;
	const QString out = runQuiet("pacman", { "-Q", package }, &code);
	return code == 0 ? out.section(' ', 1) : QString();
#else
	int code = -1;
	const QString out = runQuiet("dpkg-query", { "-W", "-f=${Status}\t${Version}", package }, &code);
	if (code != 0 || !out.startsWith("install ok installed\t")) {
		return {};
	}
	return out.section('\t', 1);
#endif
}

QString whatsnewNotesDir() {
	const QString env = qEnvironmentVariable("ZACOS9_WHATSNEW_DIR");
	return env.isEmpty() ? QStringLiteral("/usr/share/zacos9/whatsnew") : env;
}

QString whatsnewNotesFile(const QString &version) {
	return whatsnewNotesDir() + "/" + version + ".txt";
}

QString whatsnewSeenFile() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/whatsnew-seen";
}

bool whatsnewShouldShow(const QString &installed, const QString &seen) {
	if (installed.isEmpty()) {
		return false;
	}
	if (seen.isEmpty()) {
		return true;
	}
	return versionNewer(installed, seen);
}

QString debArchitecture() {
#ifdef ZACOS9_ARCH_PACKAGE
	return QStringLiteral("x86_64");
#else
	const QString arch = runQuiet("dpkg", { "--print-architecture" });
	return arch.isEmpty() ? QStringLiteral("amd64") : arch;
#endif
}
