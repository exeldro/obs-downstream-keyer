#include "downstream-keyer-dock.hpp"
#include <obs-module.h>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "downstream-keyer.hpp"
#include "name-dialog.hpp"
#include "version.h"

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("downstream-keyer", "en-US")

MODULE_EXTERN struct obs_source_info output_source_info;

bool obs_module_load()
{
	blog(LOG_INFO, "[Downstream Keyer] loaded version %s", PROJECT_VERSION);
	obs_register_source(&output_source_info);
	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_push_ui_translation(obs_module_get_string);
	obs_frontend_add_dock(new DownstreamKeyerDock(main_window));
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

void DownstreamKeyerDock::frontend_save_load(obs_data_t *save_data, bool saving,
					     void *data)
{
	auto downstreamKeyerDock = static_cast<DownstreamKeyerDock *>(data);
	if (saving) {
		downstreamKeyerDock->Save(save_data);
	} else {
		downstreamKeyerDock->Load(save_data);
	}
}

void DownstreamKeyerDock::frontend_event(enum obs_frontend_event event,
					 void *data)
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
		} else {
			downstreamKeyerDock->loadedBeforeSwitchSceneCollection =
				false;
		}
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		downstreamKeyerDock->ClearKeyers();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		downstreamKeyerDock->SceneChanged();
	}
}

DownstreamKeyerDock::DownstreamKeyerDock(QWidget *parent)
	: QDockWidget(parent), outputChannel(7)
{
	setFeatures(DockWidgetMovable | DockWidgetFloatable);
	setWindowTitle(QT_UTF8(obs_module_text("DownstreamKeyer")));
	setObjectName("DownstreamKeyerDock");
	setFloating(true);
	hide();
	tabs = new QTabWidget(this);
	tabs->setMovable(true);

	auto config = new QPushButton(this);
	config->setProperty("themeID", "configIconSmall");

	connect(config, &QAbstractButton::clicked, this,
		&DownstreamKeyerDock::ConfigClicked);

	tabs->setCornerWidget(config);
	setWidget(tabs);

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
	int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		const auto keyerData = obs_data_create();
		obs_data_set_string(keyerData, "name",
				    QT_TO_UTF8(tabs->tabText(i)));
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
			auto keyer = new DownstreamKeyer(
				outputChannel + i, QT_UTF8(obs_data_get_string(
							   keyerData, "name")));
			keyer->Load(keyerData);
			tabs->addTab(keyer, keyer->objectName());
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
	while (tabs->count()) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(0));
		tabs->removeTab(0);
		delete w;
	}
}

void DownstreamKeyerDock::AddDefaultKeyer()
{
	auto keyer = new DownstreamKeyer(
		outputChannel, QT_UTF8(obs_module_text("DefaultName")));
	tabs->addTab(keyer, keyer->objectName());
}
void DownstreamKeyerDock::SceneChanged()
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w)
			w->SceneChanged();
	}
}

void DownstreamKeyerDock::AddTransitionMenu(QMenu *tm,
					    enum transitionType transition_type)
{

	std::string transition;
	int transition_duration = 300;
	auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
	if (w) {
		transition = w->GetTransition(transition_type);
		transition_duration = w->GetTransitionDuration(transition_type);
	}

	auto setTransition = [this, transition_type](std::string name) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
		if (w)
			w->SetTransition(name.c_str(), transition_type);
	};

	auto a = tm->addAction(QT_UTF8(obs_module_text("None")));
	a->setCheckable(true);
	a->setChecked(transition.empty());
	connect(a, &QAction::triggered,
		[setTransition, transition_type] { return setTransition(""); });
	tm->addSeparator();
	obs_frontend_source_list transitions = {0};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		const char *n =
			obs_source_get_name(transitions.sources.array[i]);
		if (!n)
			continue;
		a = tm->addAction(QT_UTF8(n));
		a->setCheckable(true);
		a->setChecked(strcmp(transition.c_str(), n) == 0);
		connect(a, &QAction::triggered,
			[setTransition, n] { return setTransition(n); });
	}
	obs_frontend_source_list_free(&transitions);

	tm->addSeparator();

	QSpinBox *duration = new QSpinBox(tm);
	duration->setMinimum(50);
	duration->setSuffix("ms");
	duration->setMaximum(20000);
	duration->setSingleStep(50);
	duration->setValue(transition_duration);

	auto setDuration = [this, transition_type](int duration) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
		if (w)
			w->SetTransitionDuration(duration, transition_type);
	};
	connect(duration, (void (QSpinBox::*)(int)) & QSpinBox::valueChanged,
		setDuration);

	QWidgetAction *durationAction = new QWidgetAction(tm);
	durationAction->setDefaultWidget(duration);

	tm->addAction(durationAction);
}

void DownstreamKeyerDock::ConfigClicked()
{
	QMenu popup;
	auto *a = popup.addAction(QT_UTF8(obs_module_text("Add")));
	connect(a, SIGNAL(triggered()), this, SLOT(Add()));
	a = popup.addAction(QT_UTF8(obs_module_text("Rename")));
	connect(a, SIGNAL(triggered()), this, SLOT(Rename()));
	a = popup.addAction(QT_UTF8(obs_module_text("Remove")));
	connect(a, SIGNAL(triggered()), this, SLOT(Remove()));
	auto tm = popup.addMenu(QT_UTF8(obs_module_text("Transition")));
	AddTransitionMenu(tm, transitionType::match);
	tm = popup.addMenu(QT_UTF8(obs_module_text("ShowTransition")));
	AddTransitionMenu(tm, transitionType::show);
	tm = popup.addMenu(QT_UTF8(obs_module_text("HideTransition")));
	AddTransitionMenu(tm, transitionType::hide);
	popup.exec(QCursor::pos());
}

void DownstreamKeyerDock::Add()
{
	std::string name = obs_module_text("DefaultName");
	if (NameDialog::AskForName(this, name)) {
		auto keyer = new DownstreamKeyer(outputChannel + tabs->count(),
						 QT_UTF8(name.c_str()));
		tabs->addTab(keyer, keyer->objectName());
	}
}
void DownstreamKeyerDock::Rename()
{
	int i = tabs->currentIndex();
	if (i < 0)
		return;
	std::string name = QT_TO_UTF8(tabs->tabText(i));
	if (NameDialog::AskForName(this, name)) {
		tabs->setTabText(i, QT_UTF8(name.c_str()));
	}
}

void DownstreamKeyerDock::Remove()
{
	int i = tabs->currentIndex();
	if (i < 0)
		return;
	auto w = tabs->widget(i);
	tabs->removeTab(i);
	delete w;
	if (tabs->count() == 0) {
		AddDefaultKeyer();
	}
}
