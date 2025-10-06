#include "downstream-keyer.hpp"
#include "downstream-keyer-dock.hpp"
#include "name-dialog.hpp"
#include "obs.hpp"
#include "obs-websocket-api.h"
#include "version.h"
#include <obs-module.h>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <util/platform.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("downstream-keyer", "en-US")

MODULE_EXTERN struct obs_source_info output_source_info;

std::map<std::string, DownstreamKeyerDock *> _dsks;
obs_websocket_vendor vendor = nullptr;

extern "C" {
size_t get_view_count();
const char *get_view_name(size_t idx);
obs_view_t *get_view_by_name(const char *view_name);
obs_canvas_t *get_canvas_by_name(const char *view_name);
obs_source_t *get_source_from_view(const char *view_name, uint32_t channel);
};

size_t get_view_count()
{
	return _dsks.size();
}

const char *get_view_name(size_t idx)
{
	size_t i = 0;
	for (auto it = _dsks.begin(); it != _dsks.end(); it++) {
		if (i == idx) {
			return it->first.c_str();
		}
		i++;
	}
	return NULL;
}

obs_view_t *get_view_by_name(const char *view_name)
{
	auto it = _dsks.find(view_name);
	if (it == _dsks.end())
		return NULL;
	obs_view_t *view = it->second->GetView();
	return view;
}

obs_canvas_t *get_canvas_by_name(const char *view_name)
{
	auto it = _dsks.find(view_name);
	if (it == _dsks.end())
		return NULL;
	obs_canvas_t *canvas = it->second->GetCanvas();
	return canvas;
}

obs_source_t *get_source_from_view(const char *view_name, uint32_t channel)
{
	obs_source_t *source = nullptr;
	auto it = _dsks.find(view_name);
	if (it != _dsks.end()) {
		obs_view_t *view = it->second->GetView();
		if (view) {
			source = obs_view_get_source(view, channel);
		} else {
			obs_canvas_t *canvas = it->second->GetCanvas();
			if (canvas) {
				source = obs_canvas_get_channel(canvas, channel);
				obs_canvas_release(canvas);
			}
		}
	}
	return source;
}

obs_data_t *load_data = nullptr;

static DownstreamKeyerDock *add_dock(const char *viewName, obs_view_t *view, obs_canvas_t *canvas)
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_push_ui_translation(obs_module_get_string);
	auto dsk = new DownstreamKeyerDock(main_window, 1, view, canvas, viewName);
	QString title = QString::fromUtf8(viewName);
	title += " ";
	title += QT_UTF8(obs_module_text("DownstreamKeyer"));
	QString name = QString::fromUtf8(viewName);
	name += "DownstreamKeyerDock";

	obs_frontend_add_dock_by_id(QT_TO_UTF8(name), QT_TO_UTF8(title), dsk);
	_dsks[viewName] = dsk;
	obs_frontend_pop_ui_translation();
	if (load_data)
		DownstreamKeyerDock::frontend_save_load(load_data, false, dsk);
	return dsk;
}

static void proc_add_view(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const char *viewName = calldata_string(cd, "view_name");
	obs_view_t *view = (obs_view_t *)calldata_ptr(cd, "view");
	if (!viewName || !strlen(viewName))
		return;
	auto dski = _dsks.find(viewName);
	if (dski != _dsks.end()) {
		auto transitions = (get_transitions_callback_t)calldata_ptr(cd, "get_transitions");
		if (transitions) {
			dski->second->SetTransitions((get_transitions_callback_t)calldata_ptr(cd, "get_transitions"),
						     calldata_ptr(cd, "get_transitions_data"));
		}
		return;
	}

	auto dsk = add_dock(viewName, view, nullptr);
	dsk->SetTransitions((get_transitions_callback_t)calldata_ptr(cd, "get_transitions"),
			    calldata_ptr(cd, "get_transitions_data"));
}

static void proc_add_canvas(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const char *viewName = calldata_string(cd, "canvas_name");
	obs_canvas_t *canvas = (obs_canvas_t *)calldata_ptr(cd, "canvas");
	if (!viewName || !strlen(viewName))
		return;
	auto dski = _dsks.find(viewName);
	if (dski != _dsks.end()) {
		auto transitions = (get_transitions_callback_t)calldata_ptr(cd, "get_transitions");
		if (transitions) {
			dski->second->SetTransitions((get_transitions_callback_t)calldata_ptr(cd, "get_transitions"),
						     calldata_ptr(cd, "get_transitions_data"));
		}
		return;
	}

	auto dsk = add_dock(viewName, nullptr, canvas);
	dsk->SetTransitions((get_transitions_callback_t)calldata_ptr(cd, "get_transitions"),
			    calldata_ptr(cd, "get_transitions_data"));
}

static void proc_remove_view(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const char *viewName = calldata_string(cd, "view_name");
	if (!viewName || !strlen(viewName))
		return;
	if (_dsks.find(viewName) == _dsks.end()) {
		return;
	}
	std::string name = viewName;
	name += "DownstreamKeyerDock";
	obs_frontend_remove_dock(name.c_str());
	_dsks.erase(viewName);
}

static void proc_remove_canvas(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const char *viewName = calldata_string(cd, "canvas_name");
	if (!viewName || !strlen(viewName))
		return;
	if (_dsks.find(viewName) == _dsks.end()) {
		return;
	}
	std::string name = viewName;
	name += "DownstreamKeyerDock";
	obs_frontend_remove_dock(name.c_str());
	_dsks.erase(viewName);
}

static void refresh_canvas()
{
	obs_canvas_t *main_canvas = obs_get_main_canvas();
	obs_enum_canvases(
		[](void *param, obs_canvas_t *canvas) {
			if (canvas == param)
				return true;
			UNUSED_PARAMETER(param);
			const char *canvas_name = obs_canvas_get_name(canvas);
			if (!canvas_name || !strlen(canvas_name))
				return true;
			if (_dsks.find(canvas_name) != _dsks.end())
				return true;
			add_dock(canvas_name, nullptr, canvas);
			return true;
		},
		main_canvas);
	obs_canvas_release(main_canvas);
}

static void frontend_event(enum obs_frontend_event event, void *data)
{
	UNUSED_PARAMETER(data);
	if (event == OBS_FRONTEND_EVENT_CANVAS_ADDED) {
		refresh_canvas();
	} else if (event == OBS_FRONTEND_EVENT_CANVAS_REMOVED) {
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
		load_data = nullptr;
	}
}

static void frontend_save_load(obs_data_t *save_data, bool saving, void *data)
{
	UNUSED_PARAMETER(data);
	load_data = save_data;
	if (!saving)
		refresh_canvas();
}

bool obs_module_load()
{
	blog(LOG_INFO, "[Downstream Keyer] loaded version %s", PROJECT_VERSION);
	obs_register_source(&output_source_info);

	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_push_ui_translation(obs_module_get_string);

	auto dsk = new DownstreamKeyerDock(main_window);

	obs_frontend_add_dock_by_id("DownstreamKeyerDock", obs_module_text("DownstreamKeyer"), dsk);
	_dsks[""] = dsk;
	obs_frontend_pop_ui_translation();
	auto ph = obs_get_proc_handler();
	proc_handler_add(ph, "void downstream_keyer_add_view(in ptr view, in string view_name)", &proc_add_view, nullptr);
	proc_handler_add(ph, "void downstream_keyer_remove_view(in string view_name)", &proc_remove_view, nullptr);
	proc_handler_add(ph, "void downstream_keyer_add_canvas(in ptr canvas, in string canvas_name)", &proc_add_canvas, nullptr);
	proc_handler_add(ph, "void downstream_keyer_remove_canvas(in string canvas_name)", &proc_remove_canvas, nullptr);

	obs_frontend_add_event_callback(frontend_event, nullptr);
	obs_frontend_add_save_callback(frontend_save_load, nullptr);
	return true;
}

void obs_module_post_load(void)
{
	vendor = obs_websocket_register_vendor("downstream-keyer");
	if (!vendor)
		return;
	obs_websocket_vendor_register_request(vendor, "get_downstream_keyers", DownstreamKeyerDock::get_downstream_keyers, nullptr);
	obs_websocket_vendor_register_request(vendor, "get_downstream_keyer", DownstreamKeyerDock::get_downstream_keyer, nullptr);
	obs_websocket_vendor_register_request(vendor, "add_downstream_keyer", DownstreamKeyerDock::add_downstream_keyer, nullptr);
	obs_websocket_vendor_register_request(vendor, "remove_downstream_keyer", DownstreamKeyerDock::remove_downstream_keyer,
					      nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_get_scene", DownstreamKeyerDock::get_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_select_scene", DownstreamKeyerDock::change_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_add_scene", DownstreamKeyerDock::add_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_remove_scene", DownstreamKeyerDock::remove_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_set_tie", DownstreamKeyerDock::set_tie, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_set_transition", DownstreamKeyerDock::set_transition, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_add_exclude_scene", DownstreamKeyerDock::add_exclude_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_remove_exclude_scene", DownstreamKeyerDock::remove_exclude_scene,
					      nullptr);
}

void obs_module_unload()
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	obs_frontend_remove_save_callback(frontend_save_load, nullptr);
	_dsks.clear();
	obs_frontend_remove_dock("DownstreamKeyerDock");
	if (!vendor || !obs_get_module("obs-websocket"))
		return;
	obs_websocket_vendor_unregister_request(vendor, "get_downstream_keyers");
	obs_websocket_vendor_unregister_request(vendor, "get_downstream_keyer");
	obs_websocket_vendor_unregister_request(vendor, "add_downstream_keyer");
	obs_websocket_vendor_unregister_request(vendor, "remove_downstream_keyer");
	obs_websocket_vendor_unregister_request(vendor, "dsk_select_scene");
	obs_websocket_vendor_unregister_request(vendor, "dsk_add_scene");
	obs_websocket_vendor_unregister_request(vendor, "dsk_remove_scene");
	obs_websocket_vendor_unregister_request(vendor, "dsk_set_tie");
	obs_websocket_vendor_unregister_request(vendor, "dsk_set_transition");
	obs_websocket_vendor_unregister_request(vendor, "dsk_add_exclude_scene");
	obs_websocket_vendor_unregister_request(vendor, "dsk_remove_exclude_scene");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("DownstreamKeyer");
}

void DownstreamKeyerDock::frontend_save_load(obs_data_t *save_data, bool saving, void *data)
{
	auto downstreamKeyerDock = static_cast<DownstreamKeyerDock *>(data);
	if (saving) {
		downstreamKeyerDock->Save(save_data);
	} else {
		downstreamKeyerDock->Load(save_data);
		downstreamKeyerDock->loaded = true;
	}
}

void DownstreamKeyerDock::frontend_event(enum obs_frontend_event event, void *data)
{
	auto downstreamKeyerDock = static_cast<DownstreamKeyerDock *>(data);
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP && downstreamKeyerDock->loaded) {
		downstreamKeyerDock->ClearKeyers();
		if (!downstreamKeyerDock->closing)
			downstreamKeyerDock->AddDefaultKeyer();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		downstreamKeyerDock->ClearKeyers();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		QMetaObject::invokeMethod(downstreamKeyerDock, "SceneChanged", Qt::QueuedConnection);
	} else if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		downstreamKeyerDock->closing = true;
		downstreamKeyerDock->ClearKeyers();
	}
}

DownstreamKeyerDock::DownstreamKeyerDock(QWidget *parent, int oc, obs_view_t *v, obs_canvas_t *c, const char *vn)
	: QWidget(parent),
	  outputChannel(oc),
	  loaded(false),
	  view(v),
	  canvas(obs_canvas_get_weak_canvas(c))
{
	get_transitions = [](void *data, struct obs_frontend_source_list *sources) {
		UNUSED_PARAMETER(data);
		obs_frontend_get_transitions(sources);
	};

	if (vn)
		viewName = vn;

	if (c) {
		auto sh = obs_canvas_get_signal_handler(c);
		signal_handler_connect(
			sh, "remove",
			[](void *data, calldata_t *cd) {
				UNUSED_PARAMETER(cd);
				auto dock = static_cast<DownstreamKeyerDock *>(data);
				dock->closing = true;
				dock->ClearKeyers();
				dock->deleteLater();
			},
			this);
	}

	tabs = new QTabWidget(this);
	tabs->setMovable(true);

	connect(tabs->tabBar(), &QTabBar::tabMoved, tabs->tabBar(), [this]() {
		int count = tabs->count();
		for (int i = 0; i < count; i++) {
			auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
			w->SetOutputChannel(outputChannel + i);
		}
	});

	auto config = new QPushButton(this);
	config->setProperty("themeID", "configIconSmall");
	config->setProperty("class", "icon-gear");

	connect(config, &QAbstractButton::clicked, this, &DownstreamKeyerDock::ConfigClicked);

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

void DownstreamKeyerDock::SetTransitions(get_transitions_callback_t gt, void *gtd)
{
	get_transitions = gt;
	get_transitions_data = gtd;
}

void DownstreamKeyerDock::Save(obs_data_t *data)
{
	obs_data_array_t *keyers = obs_data_array_create();
	int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		const auto keyerData = obs_data_create();
		obs_data_set_string(keyerData, "name", QT_TO_UTF8(tabs->tabText(i)));
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
		obs_data_set_int(data, "downstream_keyers_channel", outputChannel);
		obs_data_set_array(data, "downstream_keyers", keyers);
	}
	obs_data_array_release(keyers);
}

void DownstreamKeyerDock::Load(obs_data_t *data)
{
	if (loaded)
		return;
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
		outputChannel = obs_data_get_int(data, "downstream_keyers_channel");
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
		obs_canvas_t *c = obs_weak_canvas_get_canvas(canvas);
		for (size_t i = 0; i < count; i++) {
			auto keyerData = obs_data_array_item(keyers, i);
			auto keyer = new DownstreamKeyer((int)(outputChannel + i), QT_UTF8(obs_data_get_string(keyerData, "name")),
							 view, c, get_transitions, get_transitions_data);
			keyer->Load(keyerData);
			tabs->addTab(keyer, keyer->objectName());
			obs_data_release(keyerData);
		}
		obs_canvas_release(c);
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
	loaded = false;
}

void DownstreamKeyerDock::AddDefaultKeyer()
{
	if (view) {
		if (outputChannel < 1 || outputChannel >= MAX_CHANNELS)
			outputChannel = 1;
	} else if (canvas) {
		if (outputChannel < 1 || outputChannel >= MAX_CHANNELS)
			outputChannel = 1;
	} else {
		if (outputChannel < 7 || outputChannel >= MAX_CHANNELS)
			outputChannel = 7;
	}
	obs_canvas_t *c = obs_weak_canvas_get_canvas(canvas);
	auto keyer = new DownstreamKeyer(outputChannel, QT_UTF8(obs_module_text("DefaultName")), view, c, get_transitions,
					 get_transitions_data);
	obs_canvas_release(c);
	tabs->addTab(keyer, keyer->objectName());
}
void DownstreamKeyerDock::SceneChanged()
{
	if (closing)
		return;
	const int count = tabs->count();

	obs_source_t *scene = nullptr;
	if (view) {
		obs_source_t *source = obs_view_get_source(view, 0);
		if (source && obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts = obs_transition_get_active_source(source);
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
	} else if (canvas) {
		obs_canvas_t *c = obs_weak_canvas_get_canvas(canvas);
		obs_source_t *source = c ? obs_canvas_get_channel(c, 0) : nullptr;
		obs_canvas_release(c);
		if (source && obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts = obs_transition_get_active_source(source);
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

void DownstreamKeyerDock::AddTransitionMenu(QMenu *tm, enum transitionType transition_type)
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
	connect(a, &QAction::triggered, [setTransition] { return setTransition(""); });
	tm->addSeparator();
	obs_frontend_source_list transitions = {};
	get_transitions(get_transitions_data, &transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		const char *n = obs_source_get_name(transitions.sources.array[i]);
		if (!n)
			continue;
		a = tm->addAction(QT_UTF8(n));
		a->setCheckable(true);
		a->setChecked(strcmp(transition.c_str(), n) == 0);
		connect(a, &QAction::triggered, [setTransition, n] { return setTransition(n); });
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
	connect(duration, (void(QSpinBox::*)(int)) & QSpinBox::valueChanged, setDuration);

	QWidgetAction *durationAction = new QWidgetAction(tm);
	durationAction->setDefaultWidget(duration);

	tm->addAction(durationAction);
}

void DownstreamKeyerDock::AddExcludeSceneMenu(QMenu *tm)
{
	auto setSceneExclude = [this](std::string name, bool add) {
		const auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
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
		connect(a, &QAction::triggered, [setSceneExclude, name, excluded] { return setSceneExclude(name, excluded); });
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

	tm = popup.addMenu(QT_UTF8(obs_module_text("HideAfter")));
	QSpinBox *duration = new QSpinBox(tm);
	duration->setMinimum(0);
	duration->setSuffix("ms");
	duration->setMaximum(1000000);
	duration->setSingleStep(1000);
	const auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
	duration->setValue(w->GetHideAfter());
	auto setDuration = [&](int duration) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
		if (w)
			w->SetHideAfter(duration);
	};
	connect(duration, (void(QSpinBox::*)(int)) & QSpinBox::valueChanged, setDuration);
	QWidgetAction *durationAction = new QWidgetAction(tm);
	durationAction->setDefaultWidget(duration);

	tm->addAction(durationAction);
	popup.exec(QCursor::pos());
}

void DownstreamKeyerDock::Add(QString name)
{
	if (name.isEmpty()) {
		std::string std_name = obs_module_text("DefaultName");
		if (!NameDialog::AskForName(this, std_name))
			return;
		name = QString::fromUtf8(std_name.c_str());
	}
	if (outputChannel < 7 || outputChannel >= MAX_CHANNELS)
		outputChannel = 7;
	obs_canvas_t *c = obs_weak_canvas_get_canvas(canvas);
	auto keyer = new DownstreamKeyer(outputChannel + tabs->count(), name, view, c, get_transitions, get_transitions_data);
	obs_canvas_release(c);
	tabs->addTab(keyer, keyer->objectName());
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

void DownstreamKeyerDock::Remove(int index)
{
	if (index < 0)
		index = tabs->currentIndex();
	if (index < 0)
		return;
	auto w = tabs->widget(index);
	tabs->removeTab(index);
	delete w;
	if (tabs->count() == 0) {
		AddDefaultKeyer();
	}
}
QString DownstreamKeyerDock::GetScene(QString dskName)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			return w->GetScene();
		}
	}
	return "";
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

bool DownstreamKeyerDock::AddScene(QString dskName, QString sceneName, int insertBeforeRow)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			if (w->AddScene(sceneName, insertBeforeRow)) {
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

bool DownstreamKeyerDock::SetTransition(const QString &dskName, const char *transition, int duration, transitionType tt)
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

bool DownstreamKeyerDock::AddExcludeScene(QString dskName, const char *sceneName)
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

bool DownstreamKeyerDock::RemoveExcludeScene(QString dskName, const char *sceneName)
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

void DownstreamKeyerDock::get_downstream_keyers(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end())
		return;
	_dsks[viewName]->Save(response_data);
}

void DownstreamKeyerDock::get_downstream_keyer(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
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
	obs_data_set_string(response_data, "error", "No downstream keyer with that name found");
}

void DownstreamKeyerDock::add_downstream_keyer(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	QString dskName = QString::fromUtf8(dsk_name);
	const int count = dsk->tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(dsk->tabs->widget(i));
		if (w->objectName() == dskName) {
			obs_data_set_string(response_data, "error", "'dsk_name' exists");
			obs_data_set_bool(response_data, "success", false);
			return;
		}
	}
	QMetaObject::invokeMethod(dsk, "Add", Q_ARG(QString, dskName));
	obs_data_set_bool(response_data, "success", true);
}

void DownstreamKeyerDock::remove_downstream_keyer(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	QString dskName = QString::fromUtf8(dsk_name);
	const int count = dsk->tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(dsk->tabs->widget(i));
		if (w->objectName() == dskName) {
			QMetaObject::invokeMethod(dsk, "Remove", Q_ARG(int, i));
			obs_data_set_bool(response_data, "success", true);
			return;
		}
	}
	obs_data_set_string(response_data, "error", "No downstream keyer with that name found");
}

void DownstreamKeyerDock::get_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	QString scene = dsk->GetScene(QString::fromUtf8(dsk_name));
	obs_data_set_string(response_data, "scene", scene.toUtf8().constData());
	obs_data_set_bool(response_data, "success", true);
}

void DownstreamKeyerDock::change_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
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
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success", dsk->SwitchDSK(QString::fromUtf8(dsk_name), QString::fromUtf8(scene_name)));
}

void DownstreamKeyerDock::add_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	const char *scene_name = obs_data_get_string(request_data, "scene");
	int insertBeforeRow = obs_data_get_int(request_data, "insertBeforeRow");
	if (!scene_name || !strlen(scene_name)) {
		obs_data_set_string(response_data, "error", "'scene' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error", "'dsk_name' not set");

		obs_data_set_bool(response_data, "success", false);
		return;
	}

	obs_data_set_bool(response_data, "success",
			  dsk->AddScene(QString::fromUtf8(dsk_name), QString::fromUtf8(scene_name), insertBeforeRow));
}

void DownstreamKeyerDock::remove_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
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
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success", dsk->RemoveScene(QString::fromUtf8(dsk_name), QString::fromUtf8(scene_name)));
}

void DownstreamKeyerDock::set_tie(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
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
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success", dsk->SetTie(QString::fromUtf8(dsk_name), tie));
}

void DownstreamKeyerDock::set_transition(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	auto dsk = _dsks[viewName];
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	const char *transition = obs_data_get_string(request_data, "transition");
	const char *transition_type = obs_data_get_string(request_data, "transition_type");
	long long duration = obs_data_get_int(request_data, "transition_duration");

	transitionType tt;
	if (strcmp(transition_type, "show") == 0 || strcmp(transition_type, "Show") == 0) {
		tt = transitionType::show;
	} else if (strcmp(transition_type, "hide") == 0 || strcmp(transition_type, "Hide") == 0) {
		tt = transitionType::hide;
	} else {
		tt = transitionType::match;
	}

	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	dsk->SetTransition(QString::fromUtf8(dsk_name), transition, duration, tt);
	obs_data_set_bool(response_data, "success", true);
}

void DownstreamKeyerDock::add_exclude_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
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
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success", dsk->AddExcludeScene(QString::fromUtf8(dsk_name), scene_name));
}

void DownstreamKeyerDock::remove_exclude_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
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
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_set_bool(response_data, "success", dsk->RemoveExcludeScene(QString::fromUtf8(dsk_name), scene_name));
}
