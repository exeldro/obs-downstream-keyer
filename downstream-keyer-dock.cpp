#include "downstream-keyer-dock.hpp"
#include <obs-module.h>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "downstream-keyer.hpp"
#include "name-dialog.hpp"
#include "obs-websocket-api.h"
#include "obs.hpp"
#include "version.h"

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("downstream-keyer", "en-US")

MODULE_EXTERN struct obs_source_info output_source_info;

std::map<std::string, DownstreamKeyerDock *> _dsks;
obs_websocket_vendor vendor = nullptr;

static void proc_add_view(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const char *viewName = calldata_string(cd, "view_name");
	obs_view_t *view = (obs_view_t *)calldata_ptr(cd, "view");
	if (!viewName || !strlen(viewName))
		return;
	if (_dsks.find(viewName) != _dsks.end())
		return;

	get_transitions_callback_t get_transitions =
		(get_transitions_callback_t)calldata_ptr(cd, "get_transitions");
	void *get_transitions_data = calldata_ptr(cd, "get_transitions_data");

	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_push_ui_translation(obs_module_get_string);
	auto dsk = new DownstreamKeyerDock(main_window, 1, view, viewName,
					   get_transitions,
					   get_transitions_data);
	QString title = QString::fromUtf8(viewName);
	title += " ";
	title += QT_UTF8(obs_module_text("DownstreamKeyer"));
	QString name = QString::fromUtf8(viewName);
	name += "DownstreamKeyerDock";

#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 0, 0)
	obs_frontend_add_dock_by_id(QT_TO_UTF8(name), QT_TO_UTF8(title), dsk);
#else
	auto dock = new QDockWidget(main_window);
	dock->setObjectName(name);
	dock->setWindowTitle(title);
	dock->setWidget(dsk);
	dock->setFeatures(QDockWidget::DockWidgetMovable |
			  QDockWidget::DockWidgetFloatable);
	dock->setFloating(true);
	dock->hide();
	//auto *a = static_cast<QAction *>(obs_frontend_add_dock(dock));
	obs_frontend_add_dock(dock);
#endif
	_dsks[viewName] = dsk;
	obs_frontend_pop_ui_translation();
}

static void proc_remove_view(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const char *viewName = calldata_string(cd, "view_name");
	if (!viewName || !strlen(viewName))
		return;
	if (_dsks.find(viewName) != _dsks.end())
		return;

	delete _dsks[viewName];
	_dsks.erase(viewName);
}

bool obs_module_load()
{
	blog(LOG_INFO, "[Downstream Keyer] loaded version %s", PROJECT_VERSION);
	obs_register_source(&output_source_info);
	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_push_ui_translation(obs_module_get_string);

	auto dsk = new DownstreamKeyerDock(main_window);

#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 0, 0)
	obs_frontend_add_dock_by_id("DownstreamKeyerDock",
				    obs_module_text("DownstreamKeyer"), dsk);
#else
	auto dock = new QDockWidget(main_window);
	dock->setObjectName(QString::fromUtf8("DownstreamKeyerDock"));
	dock->setWindowTitle(QT_UTF8(obs_module_text("DownstreamKeyer")));
	dock->setWidget(dsk);
	dock->setFeatures(QDockWidget::DockWidgetMovable |
			  QDockWidget::DockWidgetFloatable);
	dock->setFloating(true);
	dock->hide();
	//auto *a = static_cast<QAction *>(obs_frontend_add_dock(dock));
	obs_frontend_add_dock(dock);
#endif
	_dsks[""] = dsk;
	obs_frontend_pop_ui_translation();
	auto ph = obs_get_proc_handler();
	proc_handler_add(
		ph,
		"void downstream_keyer_add_view(in ptr view, in string view_name)",
		&proc_add_view, nullptr);
	proc_handler_add(
		ph, "void downstream_keyer_remove_view(in string view_name)",
		&proc_remove_view, nullptr);
	return true;
}

void obs_module_post_load(void)
{
	vendor = obs_websocket_register_vendor("downstream-keyer");
	if (!vendor)
		return;
	obs_websocket_vendor_register_request(
		vendor, "get_downstream_keyers",
		DownstreamKeyerDock::get_downstream_keyers, nullptr);
	obs_websocket_vendor_register_request(
		vendor, "get_downstream_keyer",
		DownstreamKeyerDock::get_downstream_keyer, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_select_scene",
					      DownstreamKeyerDock::change_scene,
					      nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_add_scene",
					      DownstreamKeyerDock::add_scene,
					      nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_remove_scene",
					      DownstreamKeyerDock::remove_scene,
					      nullptr);
	obs_websocket_vendor_register_request(
		vendor, "dsk_set_tie", DownstreamKeyerDock::set_tie, nullptr);
	obs_websocket_vendor_register_request(
		vendor, "dsk_set_transition",
		DownstreamKeyerDock::set_transition, nullptr);
	obs_websocket_vendor_register_request(
		vendor, "dsk_add_exclude_scene",
		DownstreamKeyerDock::add_exclude_scene, nullptr);
	obs_websocket_vendor_register_request(
		vendor, "dsk_remove_exclude_scene",
		DownstreamKeyerDock::remove_exclude_scene, nullptr);
}

void obs_module_unload()
{
	_dsks.clear();
	if (!vendor || !obs_get_module("obs-websocket"))
		return;
	obs_websocket_vendor_unregister_request(vendor,
						"get_downstream_keyers");
	obs_websocket_vendor_unregister_request(vendor, "get_downstream_keyer");
	obs_websocket_vendor_unregister_request(vendor, "dsk_select_scene");
	obs_websocket_vendor_unregister_request(vendor, "dsk_add_scene");
	obs_websocket_vendor_unregister_request(vendor, "dsk_remove_scene");
	obs_websocket_vendor_unregister_request(vendor, "dsk_set_tie");
	obs_websocket_vendor_unregister_request(vendor, "dsk_set_transition");
	obs_websocket_vendor_unregister_request(vendor,
						"dsk_add_exclude_scene");
	obs_websocket_vendor_unregister_request(vendor,
						"dsk_remove_exclude_scene");
}

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
		downstreamKeyerDock->loaded = true;
	}
}

void DownstreamKeyerDock::frontend_event(enum obs_frontend_event event,
					 void *data)
{
	auto downstreamKeyerDock = static_cast<DownstreamKeyerDock *>(data);
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP &&
	    downstreamKeyerDock->loaded) {
		downstreamKeyerDock->ClearKeyers();
		downstreamKeyerDock->AddDefaultKeyer();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		downstreamKeyerDock->ClearKeyers();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		downstreamKeyerDock->SceneChanged();
	}
}

DownstreamKeyerDock::DownstreamKeyerDock(QWidget *parent, int oc, obs_view_t *v,
					 const char *vn,
					 get_transitions_callback_t gt,
					 void *gtd)
	: QWidget(parent),
	  outputChannel(oc),
	  loaded(false),
	  view(v)
{
	if (gt) {
		get_transitions = gt;
		get_transitions_data = gtd;
	} else {
		get_transitions = [](void *data,
				     struct obs_frontend_source_list *sources) {
			UNUSED_PARAMETER(data);
			obs_frontend_get_transitions(sources);
		};
	}

	if (vn)
		viewName = vn;

	tabs = new QTabWidget(this);
	tabs->setMovable(true);

	connect(tabs->tabBar(), &QTabBar::tabMoved, tabs->tabBar(), [this]() {
		int count = tabs->count();
		for (int i = 0; i < count; i++) {
			auto w = dynamic_cast<DownstreamKeyer *>(
				tabs->widget(i));
			w->SetOutputChannel(outputChannel + i);
		}
	});

	auto config = new QPushButton(this);
	config->setProperty("themeID", "configIconSmall");

	connect(config, &QAbstractButton::clicked, this,
		&DownstreamKeyerDock::ConfigClicked);

	tabs->setCornerWidget(config);
	auto l = new QVBoxLayout;
	l->setContentsMargins(0, 0, 0, 0);
	l->addWidget(tabs);
	setLayout(l);

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
	if (!viewName.empty()) {
		std::string s = viewName;
		s += "_downstream_keyers_channel";
		obs_data_set_int(data, s.c_str(), outputChannel);
		s = viewName;
		s += "_downstream_keyers";
		obs_data_set_array(data, s.c_str(), keyers);

	} else {
		obs_data_set_int(data, "downstream_keyers_channel",
				 outputChannel);
		obs_data_set_array(data, "downstream_keyers", keyers);
	}
	obs_data_array_release(keyers);
}

void DownstreamKeyerDock::Load(obs_data_t *data)
{
	obs_data_array_t *keyers = nullptr;
	if (!viewName.empty()) {
		std::string s = viewName;
		s += "_downstream_keyers_channel";
		outputChannel = obs_data_get_int(data, s.c_str());
		if (outputChannel < 1 || outputChannel >= MAX_CHANNELS)
			outputChannel = 1;
		s = viewName;
		s += "_downstream_keyers";
		keyers = obs_data_get_array(data, s.c_str());
	} else {
		outputChannel =
			obs_data_get_int(data, "downstream_keyers_channel");
		if (outputChannel < 7 || outputChannel >= MAX_CHANNELS)
			outputChannel = 7;
		keyers = obs_data_get_array(data, "downstream_keyers");
	}
	ClearKeyers();
	if (keyers) {
		auto count = obs_data_array_count(keyers);
		if (count == 0) {
			AddDefaultKeyer();
		}
		for (size_t i = 0; i < count; i++) {
			auto keyerData = obs_data_array_item(keyers, i);
			auto keyer = new DownstreamKeyer(
				(int)(outputChannel + i),
				QT_UTF8(obs_data_get_string(keyerData, "name")),
				view, get_transitions, get_transitions_data);
			keyer->Load(keyerData);
			tabs->addTab(keyer, keyer->objectName());
			obs_data_release(keyerData);
		}
		obs_data_array_release(keyers);
	} else {
		AddDefaultKeyer();
	}
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
	if (view) {
		if (outputChannel < 1 || outputChannel >= MAX_CHANNELS)
			outputChannel = 1;
	} else {
		if (outputChannel < 7 || outputChannel >= MAX_CHANNELS)
			outputChannel = 7;
	}
	auto keyer = new DownstreamKeyer(
		outputChannel, QT_UTF8(obs_module_text("DefaultName")), view,
		get_transitions, get_transitions_data);
	tabs->addTab(keyer, keyer->objectName());
}
void DownstreamKeyerDock::SceneChanged()
{
	const int count = tabs->count();

	obs_source_t *scene = nullptr;
	if (view) {
		obs_source_t *source = obs_view_get_source(view, 0);
		if (source &&
		    obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts =
				obs_transition_get_active_source(source);
			if (ts) {
				obs_source_release(source);
				source = ts;
			}
		}
		if (source && obs_source_is_scene(source)) {
			scene = source;
		} else {
			obs_source_release(source);
		}
	} else {
		scene = obs_frontend_get_current_scene();
	}
	std::string scene_name = scene ? obs_source_get_name(scene) : "";
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w)
			w->SceneChanged(scene_name);
	}
	obs_source_release(scene);
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
		[setTransition] { return setTransition(""); });
	tm->addSeparator();
	obs_frontend_source_list transitions = {};
	get_transitions(get_transitions_data, &transitions);
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
	connect(duration, (void(QSpinBox::*)(int)) & QSpinBox::valueChanged,
		setDuration);

	QWidgetAction *durationAction = new QWidgetAction(tm);
	durationAction->setDefaultWidget(duration);

	tm->addAction(durationAction);
}

void DownstreamKeyerDock::AddExcludeSceneMenu(QMenu *tm)
{
	auto setSceneExclude = [this](std::string name, bool add) {
		const auto w =
			dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
		if (w) {
			if (add) {
				w->AddExcludeScene(name.c_str());
			} else {
				w->RemoveExcludeScene(name.c_str());
			}
		}
	};
	const auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *source = scenes.sources.array[i];
		auto name = obs_source_get_name(source);
		auto a = tm->addAction(QT_UTF8(name));

		a->setCheckable(true);
		bool excluded = false;
		if (w) {
			excluded = w->IsSceneExcluded(name);
		}
		a->setChecked(excluded);
		excluded = !excluded;
		connect(a, &QAction::triggered,
			[setSceneExclude, name, excluded] {
				return setSceneExclude(name, excluded);
			});
	}
	obs_frontend_source_list_free(&scenes);
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
	tm = popup.addMenu(QT_UTF8(obs_module_text("ExcludeScene")));
	AddExcludeSceneMenu(tm);
	popup.exec(QCursor::pos());
}

void DownstreamKeyerDock::Add()
{
	std::string name = obs_module_text("DefaultName");
	if (NameDialog::AskForName(this, name)) {
		if (outputChannel < 7 || outputChannel >= MAX_CHANNELS)
			outputChannel = 7;
		auto keyer = new DownstreamKeyer(outputChannel + tabs->count(),
						 QT_UTF8(name.c_str()), view,
						 get_transitions,
						 get_transitions_data);
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

bool DownstreamKeyerDock::SwitchDSK(QString dskName, QString sceneName)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			if (w->SwitchToScene(sceneName)) {
				return true;
			}
		}
	}
	return false;
}

bool DownstreamKeyerDock::AddScene(QString dskName, QString sceneName)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			if (w->AddScene(sceneName)) {
				return true;
			}
		}
	}
	return false;
}

bool DownstreamKeyerDock::RemoveScene(QString dskName, QString sceneName)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			if (w->RemoveScene(sceneName)) {
				return true;
			}
		}
	}
	return false;
}

bool DownstreamKeyerDock::SetTie(QString dskName, bool tie)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			w->SetTie(tie);
			return true;
		}
	}
	return false;
}

bool DownstreamKeyerDock::SetTransition(const QString &dskName,
					const char *transition, int duration,
					transitionType tt)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			w->SetTransition(transition, tt);
			w->SetTransitionDuration(duration, tt);
			return true;
		}
	}
	return false;
}

bool DownstreamKeyerDock::AddExcludeScene(QString dskName,
					  const char *sceneName)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			w->AddExcludeScene(sceneName);
			return true;
		}
	}
	return false;
}

bool DownstreamKeyerDock::RemoveExcludeScene(QString dskName,
					     const char *sceneName)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			w->RemoveExcludeScene(sceneName);
			return true;
		}
	}
	return false;
}

void DownstreamKeyerDock::get_downstream_keyers(obs_data_t *request_data,
						obs_data_t *response_data,
						void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end())
		return;
	_dsks[viewName]->Save(response_data);
}

void DownstreamKeyerDock::get_downstream_keyer(obs_data_t *request_data,
					       obs_data_t *response_data,
					       void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error",
				    "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error",
				    "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	QString dskName = QString::fromUtf8(dsk_name);
	const int count = dsk->tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(dsk->tabs->widget(i));
		if (w->objectName() == dskName) {
			obs_data_set_bool(response_data, "success", true);
			w->Save(response_data);
			return;
		}
	}
	obs_data_set_bool(response_data, "success", false);
	obs_data_set_string(response_data, "error",
			    "No downstream keyer with that name found");
}

void DownstreamKeyerDock::change_scene(obs_data_t *request_data,
				       obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error",
				    "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	const char *scene_name = obs_data_get_string(request_data, "scene");
	if (!scene_name) {
		obs_data_set_string(response_data, "error", "'scene' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error",
				    "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success",
			  dsk->SwitchDSK(QString::fromUtf8(dsk_name),
					 QString::fromUtf8(scene_name)));
}

void DownstreamKeyerDock::add_scene(obs_data_t *request_data,
				    obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error",
				    "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	const char *scene_name = obs_data_get_string(request_data, "scene");
	if (!scene_name || !strlen(scene_name)) {
		obs_data_set_string(response_data, "error", "'scene' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error",
				    "'dsk_name' not set");

		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success",
			  dsk->AddScene(QString::fromUtf8(dsk_name),
					QString::fromUtf8(scene_name)));
}

void DownstreamKeyerDock::remove_scene(obs_data_t *request_data,
				       obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error",
				    "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	const char *scene_name = obs_data_get_string(request_data, "scene");
	if (!scene_name || !strlen(scene_name)) {
		obs_data_set_string(response_data, "error", "'scene' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error",
				    "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success",
			  dsk->RemoveScene(QString::fromUtf8(dsk_name),
					   QString::fromUtf8(scene_name)));
}

void DownstreamKeyerDock::set_tie(obs_data_t *request_data,
				  obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error",
				    "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	if (!obs_data_has_user_value(request_data, "tie")) {
		obs_data_set_string(response_data, "error", "'tie' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	const bool tie = obs_data_get_bool(request_data, "tie");
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error",
				    "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success",
			  dsk->SetTie(QString::fromUtf8(dsk_name), tie));
}

void DownstreamKeyerDock::set_transition(obs_data_t *request_data,
					 obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error",
				    "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	const char *transition =
		obs_data_get_string(request_data, "transition");
	const char *transition_type =
		obs_data_get_string(request_data, "transition_type");
	long long duration =
		obs_data_get_int(request_data, "transition_duration");

	transitionType tt;
	if (strcmp(transition_type, "show") == 0 ||
	    strcmp(transition_type, "Show") == 0) {
		tt = transitionType::show;
	} else if (strcmp(transition_type, "hide") == 0 ||
		   strcmp(transition_type, "Hide") == 0) {
		tt = transitionType::hide;
	} else {
		tt = transitionType::match;
	}

	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error",
				    "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	dsk->SetTransition(QString::fromUtf8(dsk_name), transition, duration,
			   tt);
	obs_data_set_bool(response_data, "success", true);
}

void DownstreamKeyerDock::add_exclude_scene(obs_data_t *request_data,
					    obs_data_t *response_data,
					    void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error",
				    "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	const char *scene_name = obs_data_get_string(request_data, "scene");
	if (!scene_name || !strlen(scene_name)) {
		obs_data_set_string(response_data, "error", "'scene' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error",
				    "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success",
			  dsk->AddExcludeScene(QString::fromUtf8(dsk_name),
					       scene_name));
}

void DownstreamKeyerDock::remove_exclude_scene(obs_data_t *request_data,
					       obs_data_t *response_data,
					       void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error",
				    "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	const char *scene_name = obs_data_get_string(request_data, "scene");
	if (!scene_name || !strlen(scene_name)) {
		obs_data_set_string(response_data, "error", "'scene' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error",
				    "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success",
			  dsk->RemoveExcludeScene(QString::fromUtf8(dsk_name),
						  scene_name));
}
