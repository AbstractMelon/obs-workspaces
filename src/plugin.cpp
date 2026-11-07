// SPDX-License-Identifier: GPL-2.0-or-later

#include "layout_dialog.hpp"
#include "layout_store.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QSaveFile>
#include <QStatusBar>
#include <QUrl>

#include <utility>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-dock-layouts", "en-US")
OBS_MODULE_AUTHOR("obs-dock-layouts contributors")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Dock Layouts";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Save and load named dock layout profiles.";
}

namespace {

using docklayouts::Layout;
using docklayouts::LayoutDialog;
using docklayouts::LayoutStore;
using docklayouts::LayoutSummary;
using docklayouts::StoreError;

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString escapedMenuText(QString value)
{
	value.replace(QLatin1Char('&'), QLatin1String("&&"));
	return value;
}

QString configPath(const char *file)
{
	char *path = obs_module_config_path(file);
	const QString result = path ? QString::fromUtf8(path) : QString();
	bfree(path);
	return result;
}

QString errorText(StoreError error)
{
	switch (error) {
	case StoreError::InvalidName:
		return text("Error.InvalidName");
	case StoreError::NameExists:
		return text("Error.NameExists");
	case StoreError::InvalidLayout:
		return text("Error.InvalidLayout");
	case StoreError::NotFound:
		return text("Error.NotFound");
	case StoreError::ReadFailed:
		return text("Error.ReadFailed");
	case StoreError::InvalidFile:
		return text("Error.InvalidFile");
	case StoreError::WriteFailed:
		return text("Error.WriteFailed");
	case StoreError::None:
		break;
	}
	return {};
}

class DockLayoutController : public QObject {
public:
	DockLayoutController(QMainWindow *window, LayoutStore store, QString settingsPath)
		: QObject(window),
		  window_(window),
		  store_(std::move(store)),
		  settingsPath_(std::move(settingsPath))
	{
		loadSettings();
	}

	~DockLayoutController() { shutdown(); }

	void install()
	{
		if (!window_)
			return;

		menu_ = new QMenu(text("Menu.Title"), window_);
		menu_->setObjectName(QStringLiteral("dockLayoutsMenu"));
		connect(menu_, &QMenu::aboutToShow, this, [this] { rebuildMenu(); });

		QMenuBar *menuBar = window_->menuBar();
		QAction *before = nullptr;
		if (menuBar) {
			const auto actions = menuBar->actions();
			for (int i = actions.size() - 1; i >= 0; --i) {
				if (actions.at(i)->menu()) {
					before = actions.at(i);
					break;
				}
			}
			if (before)
				menuBar->insertMenu(before, menu_);
			else
				menuBar->addMenu(menu_);
		}

		if (!menuBar) {
			toolsAction_ = static_cast<QAction *>(
				obs_frontend_add_tools_menu_qaction(obs_module_text("Menu.Manage")));
			if (toolsAction_)
				connect(toolsAction_, &QAction::triggered, this, [this] { openManager(); });
		}

		rebuildMenu();
	}

	void shutdown()
	{
		if (shuttingDown_)
			return;
		shuttingDown_ = true;

		saveSettings();

		delete dialog_;
		dialog_ = nullptr;

		if (menu_) {
			if (window_ && window_->menuBar())
				window_->menuBar()->removeAction(menu_->menuAction());
			delete menu_;
			menu_ = nullptr;
		}

		if (toolsAction_) {
			delete toolsAction_;
			toolsAction_ = nullptr;
		}
	}

private:
	void loadSettings()
	{
		activePath_.clear();
		if (settingsPath_.isEmpty())
			return;

		QFile file(settingsPath_);
		if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
			return;

		const auto document = QJsonDocument::fromJson(file.readAll());
		if (!document.isObject())
			return;

		const QString active = QFileInfo(
					       document.object().value(QStringLiteral("active")).toString())
					       .fileName();
		if (active.isEmpty())
			return;

		const QString path = QDir(store_.directory()).absoluteFilePath(active);
		if (QFileInfo::exists(path))
			activePath_ = path;
	}

	void saveSettings()
	{
		if (settingsPath_.isEmpty())
			return;

		const QJsonObject object{
			{QStringLiteral("format"), 1},
			{QStringLiteral("active"), QFileInfo(activePath_).fileName()},
		};

		QSaveFile file(settingsPath_);
		const auto data = QJsonDocument(object).toJson(QJsonDocument::Indented);
		if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
			blog(LOG_WARNING, "[obs-dock-layouts] Could not save settings: %s",
			     file.errorString().toUtf8().constData());
	}

	void setActive(const QString &path)
	{
		activePath_ = path;
		saveSettings();
		rebuildMenu();
	}

	void refreshDialog(const QString &selectPath = {})
	{
		if (!dialog_)
			return;
		QStringList invalid;
		dialog_->refresh(store_.list(&invalid), selectPath);
		if (!invalid.isEmpty())
			blog(LOG_WARNING, "[obs-dock-layouts] Skipped %lld unreadable layout file(s)",
			     static_cast<long long>(invalid.size()));
	}

	void rebuildMenu()
	{
		if (!menu_ || shuttingDown_)
			return;

		menu_->clear();

		auto *saveAction = menu_->addAction(text("Menu.SaveCurrent"));
		connect(saveAction, &QAction::triggered, this, [this] { saveCurrent(); });

		menu_->addSeparator();

		QStringList invalid;
		const auto layouts = store_.list(&invalid);
		if (!invalid.isEmpty()) {
			blog(LOG_WARNING, "[obs-dock-layouts] Skipped %lld unreadable layout file(s)",
			     static_cast<long long>(invalid.size()));
		}

		if (layouts.isEmpty()) {
			auto *empty = menu_->addAction(text("Menu.NoLayouts"));
			empty->setEnabled(false);
		} else {
			for (const auto &layout : layouts) {
				auto *action = menu_->addAction(escapedMenuText(layout.name));
				action->setCheckable(true);
				action->setChecked(QFileInfo(layout.path) == QFileInfo(activePath_));
				action->setToolTip(layout.savedAt.toLocalTime().toString(
					QStringLiteral("yyyy-MM-dd HH:mm")));
				const QString path = layout.path;
				connect(action, &QAction::triggered, this,
					[this, path] { loadLayout(path); });
			}
		}

		menu_->addSeparator();
		auto *manageAction = menu_->addAction(text("Menu.Manage"));
		connect(manageAction, &QAction::triggered, this, [this] { openManager(); });

		auto *folderAction = menu_->addAction(text("Menu.OpenFolder"));
		connect(folderAction, &QAction::triggered, this, [this] { openFolder(); });
	}

	QString promptName(const QString &title, const QString &prompt, const QString &initial,
			   const QString &excludePath, bool rejectExisting, const QString &okText)
	{
		QInputDialog dialog(window_);
		dialog.setWindowTitle(title);
		dialog.setLabelText(prompt);
		dialog.setTextValue(initial);
		dialog.setOkButtonText(okText);
		dialog.setCancelButtonText(text("Button.Cancel"));

		while (dialog.exec() == QDialog::Accepted) {
			const QString name = dialog.textValue().trimmed().normalized(
				QString::NormalizationForm_C);
			if (!LayoutStore::validName(name)) {
				showError(text("Error.InvalidName"), &dialog);
				dialog.setTextValue(name);
				continue;
			}
			if (rejectExisting) {
				const QString existing = store_.findByName(name);
				if (!existing.isEmpty() &&
				    QFileInfo(existing) != QFileInfo(excludePath)) {
					showError(text("Error.NameExists"), &dialog);
					dialog.setTextValue(name);
					continue;
				}
			}
			return name;
		}
		return {};
	}

	void saveCurrent()
	{
		if (!window_ || shuttingDown_)
			return;

		const QString initial = QFileInfo(activePath_).completeBaseName();
		const QString name =
			promptName(text("Save.Title"), text("Save.Prompt"), initial, {}, false,
				   text("Button.Save"));
		if (name.isEmpty())
			return;

		const QString existing = store_.findByName(name);
		if (!existing.isEmpty() && !confirm(text("Save.OverwriteTitle"),
						    text("Save.Overwrite").arg(name),
						    text("Button.Replace"), text("Button.Cancel")))
			return;

		Layout layout = docklayouts::captureLayout(window_, name, obs_get_version_string());
		if (layout.dockState.isEmpty()) {
			showError(text("Error.InvalidLayout"));
			return;
		}

		QString path;
		QString detail;
		const auto result = store_.save(layout, &path, &detail);
		if (result != StoreError::None) {
			showError(errorText(result) + (detail.isEmpty() ? QString() : QStringLiteral("\n\n") + detail));
			return;
		}

		setActive(path);
		refreshDialog(path);
		notify(text("Status.Saved").arg(name));
	}

	void loadLayout(const QString &path)
	{
		if (!window_ || shuttingDown_)
			return;

		Layout layout;
		if (!store_.read(path, &layout)) {
			showError(text("Error.ReadFailed") + QStringLiteral("\n\n") +
				  QDir::toNativeSeparators(path));
			refreshDialog();
			return;
		}

		const QStringList missing = docklayouts::missingDocks(window_, layout);
		if (!missing.isEmpty()) {
			const QString message =
				text("Missing.Text").arg(missing.join(QStringLiteral("\n")));
			if (!confirm(text("Missing.Title"), message, text("Button.LoadAnyway"),
				     text("Button.Cancel")))
				return;
		}

		QString detail;
		if (!docklayouts::applyLayout(window_, layout, &detail)) {
			showError(text("Error.Restore") +
				  (detail.isEmpty() ? QString() : QStringLiteral("\n\n") + detail));
			return;
		}

		setActive(path);
		refreshDialog(path);
		notify(text("Status.Loaded").arg(layout.name));
	}

	void renameLayout(const QString &path)
	{
		Layout layout;
		if (!store_.read(path, &layout)) {
			showError(text("Error.ReadFailed") + QStringLiteral("\n\n") +
				  QDir::toNativeSeparators(path));
			refreshDialog();
			return;
		}

		const QString name =
			promptName(text("Rename.Title"), text("Rename.Prompt"), layout.name, path, true,
				   text("Button.Rename"));
		if (name.isEmpty())
			return;

		QString newPath;
		QString detail;
		const auto result = store_.rename(path, name, &newPath, &detail);
		if (result != StoreError::None) {
			showError(errorText(result) + (detail.isEmpty() ? QString() : QStringLiteral("\n\n") + detail));
			return;
		}

		if (QFileInfo(path) == QFileInfo(activePath_))
			setActive(newPath);
		else
			rebuildMenu();
		refreshDialog(newPath);
	}

	void removeLayout(const QString &path)
	{
		Layout layout;
		const QString name = store_.read(path, &layout) ? layout.name
							       : QFileInfo(path).completeBaseName();
		if (!confirm(text("Delete.Title"), text("Delete.Confirm").arg(name), text("Button.Delete"),
			     text("Button.Cancel")))
			return;

		if (!store_.remove(path)) {
			showError(text("Error.Delete"));
			refreshDialog();
			return;
		}

		if (QFileInfo(path) == QFileInfo(activePath_))
			setActive({});
		else
			rebuildMenu();
		refreshDialog();
	}

	void openManager()
	{
		if (!window_ || shuttingDown_)
			return;

		if (!dialog_) {
			dialog_ = new LayoutDialog(store_.directory(), window_);
			connect(dialog_, &LayoutDialog::saveRequested, this, [this] { saveCurrent(); });
			connect(dialog_, &LayoutDialog::loadRequested, this,
				[this](const QString &path) { loadLayout(path); });
			connect(dialog_, &LayoutDialog::renameRequested, this,
				[this](const QString &path) { renameLayout(path); });
			connect(dialog_, &LayoutDialog::deleteRequested, this,
				[this](const QString &path) { removeLayout(path); });
			connect(dialog_, &LayoutDialog::openFolderRequested, this,
				[this] { openFolder(); });
		}

		refreshDialog();
		dialog_->show();
		dialog_->raise();
		dialog_->activateWindow();
	}

	void openFolder()
	{
		QString error;
		if (!store_.ensureDirectory(&error)) {
			showError(error);
			return;
		}
		if (!QDesktopServices::openUrl(QUrl::fromLocalFile(store_.directory())))
			showError(text("Error.OpenFolder"));
	}

	void notify(const QString &message)
	{
		if (auto *bar = window_ ? window_->findChild<QStatusBar *>() : nullptr)
			bar->showMessage(message, 3000);
	}

	void showError(const QString &message, QWidget *parent = nullptr)
	{
		QMessageBox box(QMessageBox::Warning, text("Dialog.Title"), message, QMessageBox::Ok,
				parent ? parent : window_.data());
		box.setTextFormat(Qt::PlainText);
		box.exec();
	}

	bool confirm(const QString &title, const QString &message, const QString &acceptText,
		     const QString &rejectText)
	{
		QMessageBox box(QMessageBox::Question, title, message, QMessageBox::NoButton, window_);
		box.setTextFormat(Qt::PlainText);
		auto *accept = box.addButton(acceptText, QMessageBox::AcceptRole);
		auto *reject = box.addButton(rejectText, QMessageBox::RejectRole);
		box.setDefaultButton(reject);
		box.exec();
		return static_cast<QPushButton *>(box.clickedButton()) == accept;
	}

	QPointer<QMainWindow> window_;
	LayoutStore store_;
	QString settingsPath_;
	QString activePath_;
	QPointer<QMenu> menu_;
	QPointer<LayoutDialog> dialog_;
	QPointer<QAction> toolsAction_;
	bool shuttingDown_ = false;
};

QPointer<DockLayoutController> controller;

void frontendEvent(obs_frontend_event event, void *data)
{
	UNUSED_PARAMETER(data);

	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING && !controller) {
		auto *window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (!window) {
			blog(LOG_WARNING, "[obs-dock-layouts] OBS main window is not available");
			return;
		}

		const QString directory = configPath("layouts");
		const QString settings = configPath("settings.json");
		if (directory.isEmpty()) {
			blog(LOG_WARNING, "[obs-dock-layouts] Could not resolve the plugin config path");
			return;
		}

		LayoutStore store(directory);
		QString error;
		if (!store.ensureDirectory(&error))
			blog(LOG_WARNING, "[obs-dock-layouts] %s", error.toUtf8().constData());

		controller = new DockLayoutController(window, std::move(store), settings);
		controller->install();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		delete controller;
		controller = nullptr;
	}
}

} // namespace

bool obs_module_load(void)
{
	obs_frontend_add_event_callback(frontendEvent, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
	delete controller;
	controller = nullptr;
}
