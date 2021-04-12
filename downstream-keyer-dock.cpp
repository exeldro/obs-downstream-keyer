#include "downstream-keyer-dock.hpp"
#include <obs-module.h>
#include <QMainWindow>
#include <QVBoxLayout>

#include "downstream-keyer.hpp"

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("media-controls", "en-US")

bool obs_module_load()
{
	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_push_ui_translation(obs_module_get_string);
	auto downstreamKeyerDock = new DownstreamKeyerDock(main_window);
	obs_frontend_add_dock(downstreamKeyerDock);
	obs_frontend_pop_ui_translation();
	return true;
}

void obs_module_unload() {}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("DownstreamKeyer");
}

static void frontend_save_load(obs_data_t *save_data, bool saving, void *data)
{
	auto downstreamKeyerDock = static_cast<DownstreamKeyerDock *>(data);
	if (saving) {
		downstreamKeyerDock->Save(save_data);
	} else {
		downstreamKeyerDock->Load(save_data);
	}
}

static void frontend_event(enum obs_frontend_event event, void *data)
{
	auto downstreamKeyerDock = static_cast<DownstreamKeyerDock *>(data);
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
		downstreamKeyerDock->ClearKeyers();
		downstreamKeyerDock->AddDefaultKeyer();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		// for as long as OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP does not work
		if (!downstreamKeyerDock->loadedBeforeSwitchSceneCollection) {
			downstreamKeyerDock->ClearKeyers();
			downstreamKeyerDock->AddDefaultKeyer();
		}else {
			downstreamKeyerDock->loadedBeforeSwitchSceneCollection =
				false;
		}
	}
}

DownstreamKeyerDock::DownstreamKeyerDock(QWidget *parent) : QDockWidget(parent)
{
	setWindowTitle(QT_UTF8(obs_module_text("DownstreamKeyer")));
	setObjectName("DownstreamKeyerDock");
	setFloating(true);
	hide();
	mainLayout = new QVBoxLayout(this);
	mainLayout->setSpacing(0);
	mainLayout->setContentsMargins(0, 0, 0, 0);

	auto *dockWidgetContents = new QWidget;
	dockWidgetContents->setLayout(mainLayout);
	setWidget(dockWidgetContents);

	obs_frontend_add_save_callback(frontend_save_load, this);
	obs_frontend_add_event_callback(frontend_event, this);
}

DownstreamKeyerDock::~DownstreamKeyerDock()
{
	obs_frontend_remove_save_callback(frontend_save_load, this);
	obs_frontend_remove_event_callback(frontend_event, this);
	ClearKeyers();
}

void DownstreamKeyerDock::Save(obs_data_t *data)
{
	obs_data_set_int(data, "downstream_keyers_channel", outputChannel);
	obs_data_array_t *keyers = obs_data_array_create();
	int count = mainLayout->count();
	for (int i = 0; i < count; i++) {
		QLayoutItem *item = mainLayout->itemAt(i);
		auto w = dynamic_cast<DownstreamKeyer *>(item->widget());
		const auto keyerData = obs_data_create();
		w->Save(keyerData);
		obs_data_array_push_back(keyers, keyerData);
		obs_data_release(keyerData);
	}
	obs_data_set_array(data, "downstream_keyers", keyers);
	obs_data_array_release(keyers);
}

void DownstreamKeyerDock::Load(obs_data_t *data)
{
	outputChannel = obs_data_get_int(data, "downstream_keyers_channel");
	if (outputChannel < 7)
		outputChannel = 7;
	ClearKeyers();
	obs_data_array_t *keyers =
		obs_data_get_array(data, "downstream_keyers");
	if (keyers) {
		auto count = obs_data_array_count(keyers);
		if (count == 0) {
			AddDefaultKeyer();
		}
		for (size_t i = 0; i < count; i++) {
			auto keyerData = obs_data_array_item(keyers, i);
			auto keyer = new DownstreamKeyer(outputChannel + i);
			keyer->Load(keyerData);
			mainLayout->addWidget(keyer);
			obs_data_release(keyerData);
		}
		obs_data_array_release(keyers);
	} else {
		AddDefaultKeyer();
	}
	loadedBeforeSwitchSceneCollection = true;
}

void DownstreamKeyerDock::ClearKeyers()
{
	while (mainLayout->count()) {
		QLayoutItem *item = mainLayout->itemAt(0);
		auto w = dynamic_cast<DownstreamKeyer *>(item->widget());
		mainLayout->removeItem(item);
		delete item;
		delete w;
	}
}

void DownstreamKeyerDock::AddDefaultKeyer()
{
	mainLayout->addWidget(new DownstreamKeyer(outputChannel));
}
