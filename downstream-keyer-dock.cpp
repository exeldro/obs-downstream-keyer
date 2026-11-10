#include "downstream-keyer.hpp"
#include "downstream-keyer-dock.hpp"
#include "name-dialog.hpp"
#include "obs.hpp"
#include "obs-websocket-api.h"
#include "version.h"
#include <obs-module.h>
#include <algorithm>
#include <functional>
#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QTabBar>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <util/platform.h>
#include <vector>

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

// Keyer tab widget whose own tab bar stays hidden: the dock shows the tabs as a grid of buttons instead (three to a
// row) and is told when tabs are added or removed. The hidden tab bar still holds the order, names and tally colours.
class KeyerTabWidget : public QTabWidget {
public:
	explicit KeyerTabWidget(QWidget *parent) : QTabWidget(parent) { tabBar()->hide(); }
	std::function<void()> tabsChanged;

protected:
	void tabInserted(int) override
	{
		if (tabsChanged)
			tabsChanged();
	}
	void tabRemoved(int) override
	{
		if (tabsChanged)
			tabsChanged();
	}
};

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
	if (it == _dsks.end()) {
		return NULL;
	}
	obs_view_t *view = it->second->GetView();
	return view;
}

obs_canvas_t *get_canvas_by_name(const char *view_name)
{
	auto it = _dsks.find(view_name);
	if (it == _dsks.end()) {
		return NULL;
	}
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
				if (obs_canvas_removed(canvas)) {
					std::string name = view_name;
					name += "DownstreamKeyerDock";
					obs_frontend_remove_dock(name.c_str());
					_dsks.erase(view_name);
				} else {
					source = obs_canvas_get_channel(canvas, channel);
				}
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
	if (load_data) {
		DownstreamKeyerDock::frontend_save_load(load_data, false, dsk);
	}
	return dsk;
}

static void proc_add_view(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const char *viewName = calldata_string(cd, "view_name");
	obs_view_t *view = (obs_view_t *)calldata_ptr(cd, "view");
	if (!viewName || !strlen(viewName)) {
		return;
	}
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
	if (!viewName || !strlen(viewName)) {
		return;
	}
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
	if (!viewName || !strlen(viewName)) {
		return;
	}
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
	if (!viewName || !strlen(viewName)) {
		return;
	}
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
			if (canvas == param) {
				return true;
			}
			UNUSED_PARAMETER(param);
			const char *canvas_name = obs_canvas_get_name(canvas);
			if (!canvas_name || !strlen(canvas_name)) {
				return true;
			}
			if (_dsks.find(canvas_name) != _dsks.end()) {
				return true;
			}
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
	} else if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING || event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		// preview scene items left in a saved scene collection (OBS closed or crashed), then show the current ones
		dsk_purge_preview_items();
		for (auto &dsk : _dsks)
			dsk.second->RefreshPreviewOverlays();
	}
}

static void frontend_save_load(obs_data_t *save_data, bool saving, void *data)
{
	UNUSED_PARAMETER(data);
	load_data = save_data;
	if (!saving) {
		refresh_canvas();
	}
}

bool obs_module_load()
{
	blog(LOG_INFO, "[Downstream Keyer] loaded version %s", PROJECT_VERSION);
	obs_register_source(&output_source_info);
	dsk_register_video_only_source();

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
	if (!vendor) {
		return;
	}
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
	obs_websocket_vendor_register_request(vendor, "dsk_add_load_scene", DownstreamKeyerDock::add_load_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_remove_load_scene", DownstreamKeyerDock::remove_load_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_go_live", DownstreamKeyerDock::go_live, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_go_to_preview", DownstreamKeyerDock::go_to_preview, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_all_keyers_off", DownstreamKeyerDock::all_keyers_off, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_set_hold_keyers", DownstreamKeyerDock::set_hold_keyers, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_set_keyer_off_at_scene_change",
					      DownstreamKeyerDock::set_keyer_off_at_scene_change, nullptr);
	obs_websocket_vendor_register_request(vendor, "dsk_transition_keyers", DownstreamKeyerDock::transition_keyers, nullptr);
}

void obs_module_unload()
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	obs_frontend_remove_save_callback(frontend_save_load, nullptr);
	_dsks.clear();
	obs_frontend_remove_dock("DownstreamKeyerDock");
	if (!vendor || !obs_get_module("obs-websocket")) {
		return;
	}
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
	obs_websocket_vendor_unregister_request(vendor, "dsk_add_load_scene");
	obs_websocket_vendor_unregister_request(vendor, "dsk_remove_load_scene");
	obs_websocket_vendor_unregister_request(vendor, "dsk_go_live");
	obs_websocket_vendor_unregister_request(vendor, "dsk_go_to_preview");
	obs_websocket_vendor_unregister_request(vendor, "dsk_all_keyers_off");
	obs_websocket_vendor_unregister_request(vendor, "dsk_set_hold_keyers");
	obs_websocket_vendor_unregister_request(vendor, "dsk_set_keyer_off_at_scene_change");
	obs_websocket_vendor_unregister_request(vendor, "dsk_transition_keyers");
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
		if (!downstreamKeyerDock->closing) {
			downstreamKeyerDock->AddDefaultKeyer();
		}
	} else if (event == OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED || event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		if (!downstreamKeyerDock->view && !downstreamKeyerDock->canvas)
			downstreamKeyerDock->HookTransitions();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		downstreamKeyerDock->ClearKeyers();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED || event == OBS_FRONTEND_EVENT_TRANSITION_STOPPED) {
		// the program transition is done: take cues live, then show the next look on the preview again
		dsk_set_program_transition_active(false);
		downstreamKeyerDock->ForEachKeyer([](DownstreamKeyer *w) { w->CheckPreviewTake(true); });
		// after a program transition no preview copy belongs in the program scene (a cue was just taken above)
		if (!downstreamKeyerDock->view && !downstreamKeyerDock->canvas) {
			obs_source_t *program = dsk_get_main_program_scene();
			if (program)
				dsk_remove_preview_items(program, nullptr);
			obs_source_release(program);
		}
		if (downstreamKeyerDock->offAtSceneChangeUsed) {
			downstreamKeyerDock->offAtSceneChangeUsed = false;
			downstreamKeyerDock->SetOffAtSceneChange(false);
		}
		downstreamKeyerDock->RefreshPreviewOverlays(true);
		if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED)
			QMetaObject::invokeMethod(downstreamKeyerDock, "SceneChanged", Qt::QueuedConnection);
	} else if (event == OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED) {
		downstreamKeyerDock->ForEachKeyer([](DownstreamKeyer *w) { w->PreviewSceneChanged(); });
		downstreamKeyerDock->UpdateTabTally();
	} else if (event == OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED || event == OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED) {
		downstreamKeyerDock->ForEachKeyer([](DownstreamKeyer *w) { w->StudioModeChanged(); });
		downstreamKeyerDock->UpdateAllKeyersButtons();
		downstreamKeyerDock->UpdateTabTally();
	} else if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		downstreamKeyerDock->closing = true;
		downstreamKeyerDock->ClearKeyers();
	}
}

DownstreamKeyerDock::DownstreamKeyerDock(QWidget *parent, int oc, obs_view_t *v, obs_canvas_t *c, const char *vn)
	: QFrame(parent),
	  outputChannel(oc),
	  loaded(false),
	  view(v),
	  canvas(obs_canvas_get_weak_canvas(c))
{
	get_transitions = [](void *data, struct obs_frontend_source_list *sources) {
		UNUSED_PARAMETER(data);
		obs_frontend_get_transitions(sources);
	};

	if (vn) {
		viewName = vn;
	}

	if (c) {
		auto sh = obs_canvas_get_signal_handler(c);
		signal_handler_connect(sh, "remove", remove_canvas, this);
	}

	auto keyerTabs = new KeyerTabWidget(this);
	tabs = keyerTabs;

	connect(tabs->tabBar(), &QTabBar::tabMoved, tabs->tabBar(), [this]() {
		int count = tabs->count();
		for (int i = 0; i < count; i++) {
			auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
			w->SetOutputChannel(outputChannel + i);
		}
		UpdateTabTally();
	});

	// the keyer tabs, shown as a grid of buttons three to a row
	tabGrid = new QGridLayout;
	tabGrid->setContentsMargins(4, 0, 4, 0);
	tabGrid->setSpacing(4);
	keyerTabs->tabsChanged = [this]() {
		UpdateTabTally();
	};
	connect(tabs, &QTabWidget::currentChanged, this, [this]() { UpdateTabTally(); });

	// Global Keyer Actions: buttons above the tabs that act on every keyer in this dock
	auto allKeyersTitle = new QLabel(QT_UTF8(obs_module_text("AllKeyersTitle")), this);
	QFont titleFont = allKeyersTitle->font();
	titleFont.setBold(true);
	allKeyersTitle->setFont(titleFont);

	allKeyersOffButton = new QPushButton(QT_UTF8(obs_module_text("AllKeyersOff")), this);
	connect(allKeyersOffButton, &QPushButton::clicked, this, &DownstreamKeyerDock::AllKeyersOff);

	offAtSceneChangeButton = new QPushButton(QT_UTF8(obs_module_text("SceneChangeOff")), this);
	offAtSceneChangeButton->setCheckable(true);
	offAtSceneChangeButton->setToolTip(QT_UTF8(obs_module_text("SceneChangeOffTip")));
	connect(offAtSceneChangeButton, &QPushButton::clicked, this, &DownstreamKeyerDock::SetOffAtSceneChange);

	holdKeyersButton = new QPushButton(QT_UTF8(obs_module_text("HoldKeyers")), this);
	holdKeyersButton->setCheckable(true);
	holdKeyersButton->setToolTip(QT_UTF8(obs_module_text("HoldKeyersTip")));
	connect(holdKeyersButton, &QPushButton::clicked, this, &DownstreamKeyerDock::SetHoldKeyers);

	transitionKeyersButton = new QPushButton(QT_UTF8(obs_module_text("TransitionKeyers")), this);
	transitionKeyersButton->setToolTip(QT_UTF8(obs_module_text("TransitionKeyersTip")));
	connect(transitionKeyersButton, &QPushButton::clicked, this, &DownstreamKeyerDock::TransitionKeyers);

	// 2x2 grid so the dock can be narrower than a single row of four buttons; outside studio mode All Keyers Off
	// takes the whole top row (see UpdateAllKeyersButtons)
	allKeyersGrid = new QGridLayout;
	allKeyersGrid->setContentsMargins(4, 0, 4, 0);
	allKeyersGrid->setSpacing(4);
	allKeyersGrid->addWidget(allKeyersOffButton, 0, 0);
	allKeyersGrid->addWidget(transitionKeyersButton, 0, 1);
	allKeyersGrid->addWidget(offAtSceneChangeButton, 1, 0);
	allKeyersGrid->addWidget(holdKeyersButton, 1, 1);
	allKeyersGrid->setColumnStretch(0, 1);
	allKeyersGrid->setColumnStretch(1, 1);

	auto l = new QVBoxLayout;
	l->setContentsMargins(0, 0, 0, 0);
	l->setSpacing(4);
	auto titleRow = new QHBoxLayout;
	titleRow->setContentsMargins(4, 4, 4, 0);
	titleRow->addWidget(allKeyersTitle);
	l->addLayout(titleRow);
	l->addLayout(allKeyersGrid);
	// header for the keyer tabs, set apart from the global buttons above
	auto keyersTitle = new QLabel(QT_UTF8(obs_module_text("KeyersTitle")), this);
	keyersTitle->setFont(titleFont);
	auto keyersTitleRow = new QHBoxLayout;
	keyersTitleRow->setContentsMargins(4, 4, 4, 0);
	keyersTitleRow->addWidget(keyersTitle);
	l->addLayout(keyersTitleRow);
	l->addLayout(tabGrid);
	l->addWidget(tabs);
	setLayout(l);
	UpdateAllKeyersButtons();

	obs_frontend_add_save_callback(frontend_save_load, this);
	obs_frontend_add_event_callback(frontend_event, this);
	if (!view && !canvas)
		HookTransitions();
}

void DownstreamKeyerDock::UnhookTransitions()
{
	for (auto weak : hookedTransitions) {
		obs_source_t *tr = obs_weak_source_get_source(weak);
		if (tr)
			signal_handler_disconnect(obs_source_get_signal_handler(tr), "transition_start", transition_start, this);
		obs_source_release(tr);
		obs_weak_source_release(weak);
	}
	hookedTransitions.clear();
}

// (re)connect to every OBS scene transition, they are private so the global source_transition_start does not fire
void DownstreamKeyerDock::HookTransitions()
{
	UnhookTransitions();
	obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		obs_source_t *tr = transitions.sources.array[i];
		signal_handler_connect(obs_source_get_signal_handler(tr), "transition_start", transition_start, this);
		hookedTransitions.push_back(obs_source_get_weak_source(tr));
	}
	obs_frontend_source_list_free(&transitions);
}

// The main program transition started: the keyers change with it instead of after it.
void DownstreamKeyerDock::transition_start(void *data, calldata_t *cd)
{
	auto dock = static_cast<DownstreamKeyerDock *>(data);
	auto tr = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	obs_source_t *program = obs_get_output_source(0);
	const bool isProgram = tr && tr == program;
	obs_source_release(program);
	if (!isProgram)
		return;
	obs_source_t *dest = obs_transition_get_source(tr, OBS_TRANSITION_SOURCE_B);
	if (!dest) // fade to black
		return;
	dsk_set_program_transition_active(true);
	// the frontend starts transitions on the UI thread: handle it right away, before the preview copies are drawn
	if (QThread::currentThread() == dock->thread()) {
		dock->ProgramTransitionStarting(dest);
		obs_source_release(dest);
		return;
	}
	QMetaObject::invokeMethod(
		dock,
		[dock, dest]() {
			dock->ProgramTransitionStarting(dest);
			obs_source_release(dest);
		},
		Qt::QueuedConnection);
}

void DownstreamKeyerDock::ProgramTransitionStarting(obs_source_t *dest)
{
	if (closing)
		return;
	const std::string scene = obs_source_get_name(dest);
	transitionStartedScene = scene;
	// work out the All Keyers context first: whether a cue is being taken depends on the cued preview items
	const auto ctx = GetSceneChangeContext(scene, false);
	if (offAtSceneChange && !holdKeyers)
		offAtSceneChangeUsed = true; // turned off when the transition ends
	ForEachKeyer([dest](DownstreamKeyer *w) { w->ProgramTransitionStarting(dest); });
	ForEachKeyer([&scene, &ctx](DownstreamKeyer *w) { w->SceneChanged(scene, ctx); });
	UpdateTabTally();
}

void DownstreamKeyerDock::RefreshPreviewOverlays(bool force)
{
	ForEachKeyer([force](DownstreamKeyer *w) { w->UpdatePreviewOverlay(force); });
	UpdateTabTally();
}

// tab tally: the tab is filled red while the keyer is on program, green while it shows on the studio mode preview
void DownstreamKeyerDock::UpdateTabTally()
{
	if (closing)
		return;
	for (int i = 0; i < tabs->count(); i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (!w)
			continue;
		QVariant tally;
		if (w->IsLiveOnOutput())
			tally = QColor(198, 40, 40);
		else if (w->IsOnPreview())
			tally = QColor(46, 125, 50);
		tabs->tabBar()->setTabData(i, tally);
	}
	SyncTabButtons();
}

// the keyer tabs as buttons, three to a row: a button never gets narrower than its name, so neither does the dock
void DownstreamKeyerDock::SyncTabButtons()
{
	if (closing)
		return;
	const int count = tabs->count();
	if ((int)tabButtons.size() != count) {
		for (auto b : tabButtons) {
			tabGrid->removeWidget(b);
			b->hide();
			b->deleteLater();
		}
		tabButtons.clear();
		for (auto b : tabBlanks) {
			tabGrid->removeWidget(b);
			b->deleteLater();
		}
		tabBlanks.clear();
		const int columns = 3;
		// with fewer than three keyers the row is filled with blank tabs, so a tab is always a third of the width
		for (int i = count; i < columns; i++) {
			auto b = new QPushButton(this);
			b->setEnabled(false);
			tabGrid->addWidget(b, 0, i);
			tabBlanks.push_back(b);
		}
		for (int i = 0; i < count; i++) {
			auto b = new QPushButton(this);
			b->setCheckable(true);
			b->installEventFilter(this);
			connect(b, &QPushButton::clicked, this, [this, b]() {
				auto it = std::find(tabButtons.begin(), tabButtons.end(), b);
				if (it != tabButtons.end())
					tabs->setCurrentIndex((int)(it - tabButtons.begin()));
				SyncTabButtons();
			});
			tabGrid->addWidget(b, i / columns, i % columns);
			tabButtons.push_back(b);
		}
		for (int c = 0; c < columns; c++)
			tabGrid->setColumnStretch(c, 1);
	}
	const int current = tabs->currentIndex();
	for (int i = 0; i < count; i++) {
		auto b = tabButtons[i];
		b->setText(tabs->tabText(i));
		b->setChecked(i == current);
		const QColor tally = tabs->tabBar()->tabData(i).value<QColor>();
		QString style;
		if (tally.isValid()) {
			style = QString("background-color: %1; color: white; font-weight: bold;").arg(tally.name());
			// keep the selected tab recognisable
			if (i == current)
				style += " border: 2px solid white;";
		}
		b->setStyleSheet(style);
	}
}

// drag a keyer tab button onto another to move it there, as the tab bar allowed
bool DownstreamKeyerDock::eventFilter(QObject *obj, QEvent *event)
{
	auto it = std::find(tabButtons.begin(), tabButtons.end(), obj);
	if (it != tabButtons.end()) {
		const int index = (int)(it - tabButtons.begin());
		if (event->type() == QEvent::MouseButtonPress) {
			auto me = static_cast<QMouseEvent *>(event);
			if (me->button() == Qt::LeftButton) {
				dragFrom = index;
				dragStart = me->globalPosition().toPoint();
			}
		} else if (event->type() == QEvent::MouseButtonRelease && dragFrom == index) {
			dragFrom = -1;
			const QPoint pos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
			if ((pos - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
				for (int to = 0; to < (int)tabButtons.size(); to++) {
					if (to == index || !tabButtons[to]->rect().contains(tabButtons[to]->mapFromGlobal(pos)))
						continue;
					QMetaObject::invokeMethod(
						this,
						[this, index, to]() {
							tabs->tabBar()->moveTab(index, to);
							tabs->setCurrentIndex(to);
							UpdateTabTally();
						},
						Qt::QueuedConnection);
					break;
				}
			}
		}
	}
	return QFrame::eventFilter(obj, event);
}

SceneChangeContext DownstreamKeyerDock::GetSceneChangeContext(const std::string &scene, bool forPreview)
{
	SceneChangeContext ctx;
	ctx.hold = holdKeyers;
	bool cueTaken = false;
	ForEachKeyer([&](DownstreamKeyer *w) {
		if (forPreview ? w->IsPreviewing() : w->IsPreviewTakeTo(scene))
			cueTaken = true;
	});
	ctx.clearOthers = offAtSceneChange || cueTaken;
	return ctx;
}

void DownstreamKeyerDock::SetupKeyer(DownstreamKeyer *keyer)
{
	keyer->SetContextProvider([this](const std::string &scene,
					 bool forPreview) { return GetSceneChangeContext(scene, forPreview); },
				  [this]() { RefreshPreviewOverlays(false); });
	keyer->SetHeld(holdKeyers);

	// the settings gear sits at the right of the keyer's bottom toolbar, so the tabs can use the full width
	auto config = new QPushButton(keyer);
	config->setProperty("themeID", "configIconSmall");
	config->setProperty("class", "icon-gear");
	connect(config, &QAbstractButton::clicked, this, &DownstreamKeyerDock::ConfigClicked);
	keyer->AddToolbarWidget(config);
}

void DownstreamKeyerDock::AllKeyersOff()
{
	ForEachKeyer([](DownstreamKeyer *w) { w->KeyerOff(); });
}

void DownstreamKeyerDock::SetOffAtSceneChange(bool on)
{
	offAtSceneChange = on;
	UpdateAllKeyersButtons();
	RefreshPreviewOverlays(false);
}

void DownstreamKeyerDock::SetHoldKeyers(bool on)
{
	holdKeyers = on;
	UpdateAllKeyersButtons();
	// stops any Hide After countdown while held; when released, keyers that stay live start it again
	ForEachKeyer([on](DownstreamKeyer *w) { w->SetHeld(on); });
	if (!on) {
		// match the Load On Scene keyers of the program scene, everything else goes off
		const auto scene = ProgramSceneName();
		ForEachKeyer([&scene](DownstreamKeyer *w) { w->SyncToProgramScene(scene); });
	}
	RefreshPreviewOverlays(false);
}

void DownstreamKeyerDock::UpdateAllKeyersButtons()
{
	offAtSceneChangeButton->setChecked(offAtSceneChange);
	holdKeyersButton->setChecked(holdKeyers);
	// Hold Keyers overrides Keyer Off at Scene Change
	offAtSceneChangeButton->setEnabled(!holdKeyers);
	// the main output only: studio mode does not apply to views and canvases
	const bool studio = !view && !canvas && obs_frontend_preview_program_mode_active();
	transitionKeyersButton->setVisible(studio);
	// without Transition Keyers, All Keyers Off takes the whole top row
	allKeyersGrid->removeWidget(allKeyersOffButton);
	allKeyersGrid->addWidget(allKeyersOffButton, 0, 0, 1, studio ? 1 : 2);
}

void DownstreamKeyerDock::TransitionKeyers()
{
	if (view || canvas || !obs_frontend_preview_program_mode_active())
		return;
	obs_source_t *previewScene = obs_frontend_get_current_preview_scene();
	const std::string scene = previewScene ? obs_source_get_name(previewScene) : "";
	obs_source_release(previewScene);
	if (scene.empty())
		return;
	// work out every keyer's next look first, taking cues changes the context for the others
	const auto ctx = GetSceneChangeContext(scene, true);
	std::vector<std::pair<DownstreamKeyer *, QStringList>> next;
	ForEachKeyer([&](DownstreamKeyer *w) { next.emplace_back(w, w->PredictedScenes(scene, ctx)); });
	for (auto &[w, scenes] : next) {
		if (w->IsPreviewing())
			w->GoLive(); // the cue goes live and is cleared
		else if (scenes.isEmpty())
			w->KeyerOff();
		else
			w->TakeLive(scenes);
	}
}

DownstreamKeyerDock::~DownstreamKeyerDock()
{
	// the tab buttons are not kept in sync while the keyers are taken down
	closing = true;
	if (!viewName.empty()) {
		_dsks.erase(viewName);
	}
	obs_frontend_remove_save_callback(frontend_save_load, this);
	obs_frontend_remove_event_callback(frontend_event, this);
	if (!view && !canvas)
		UnhookTransitions();
	ClearKeyers();
	auto c = obs_weak_canvas_get_canvas(canvas);
	if (c) {
		auto sh = obs_canvas_get_signal_handler(c);
		signal_handler_disconnect(sh, "remove", remove_canvas, this);
		obs_canvas_release(c);
	}
	obs_weak_canvas_release(canvas);
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
	const std::string prefix = viewName.empty() ? "" : viewName + "_";
	obs_data_set_bool(data, (prefix + "downstream_keyers_hold").c_str(), holdKeyers);
	obs_data_set_bool(data, (prefix + "downstream_keyers_off_at_scene_change").c_str(), offAtSceneChange);
}

void DownstreamKeyerDock::Load(obs_data_t *data)
{
	if (loaded) {
		return;
	}
	obs_data_array_t *keyers = nullptr;
	if (!viewName.empty()) {
		std::string s = viewName;
		s += "_downstream_keyers_channel";
		outputChannel = obs_data_get_int(data, s.c_str());
		if (outputChannel < 1 || outputChannel >= MAX_CHANNELS) {
			outputChannel = 1;
		}
		s = viewName;
		s += "_downstream_keyers";
		keyers = obs_data_get_array(data, s.c_str());
	} else {
		outputChannel = obs_data_get_int(data, "downstream_keyers_channel");
		if (outputChannel < 7 || outputChannel >= MAX_CHANNELS) {
			outputChannel = 7;
		}
		keyers = obs_data_get_array(data, "downstream_keyers");
	}
	const std::string prefix = viewName.empty() ? "" : viewName + "_";
	holdKeyers = obs_data_get_bool(data, (prefix + "downstream_keyers_hold").c_str());
	offAtSceneChange = obs_data_get_bool(data, (prefix + "downstream_keyers_off_at_scene_change").c_str());
	UpdateAllKeyersButtons();
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
			SetupKeyer(keyer);
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
		if (outputChannel < 1 || outputChannel >= MAX_CHANNELS) {
			outputChannel = 1;
		}
	} else if (canvas) {
		if (outputChannel < 1 || outputChannel >= MAX_CHANNELS) {
			outputChannel = 1;
		}
	} else {
		if (outputChannel < 7 || outputChannel >= MAX_CHANNELS) {
			outputChannel = 7;
		}
	}
	obs_canvas_t *c = obs_weak_canvas_get_canvas(canvas);
	auto keyer = new DownstreamKeyer(outputChannel, QT_UTF8(obs_module_text("DefaultName")), view, c, get_transitions,
					 get_transitions_data);
	obs_canvas_release(c);
	SetupKeyer(keyer);
	tabs->addTab(keyer, keyer->objectName());
}
// the program scene of this dock: the main program scene, or the active scene of its view / canvas
std::string DownstreamKeyerDock::ProgramSceneName()
{
	obs_source_t *scene = nullptr;
	if (view) {
		obs_source_t *source = obs_view_get_source(view, 0);
		while (source && obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts = obs_transition_get_active_source(source);
			if (ts) {
				obs_source_release(source);
				source = ts;
			} else {
				break;
			}
		}
		if (source && obs_source_is_scene(source)) {
			scene = source;
		} else {
			obs_source_release(source);
		}
	} else if (canvas) {
		obs_canvas_t *c = obs_weak_canvas_get_canvas(canvas);
		obs_source_t *source = c && !obs_canvas_removed(c) ? obs_canvas_get_channel(c, 0) : nullptr;
		obs_canvas_release(c);
		while (source && obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts = obs_transition_get_active_source(source);
			if (ts) {
				obs_source_release(source);
				source = ts;
			} else {
				break;
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
	obs_source_release(scene);
	return scene_name;
}

void DownstreamKeyerDock::SceneChanged()
{
	if (closing) {
		return;
	}
	const std::string scene_name = ProgramSceneName();
	if (!view && !canvas && !transitionStartedScene.empty() && scene_name == transitionStartedScene) {
		// already handled when the transition to this scene started
		transitionStartedScene.clear();
		return;
	}
	const auto ctx = GetSceneChangeContext(scene_name, false);
	ForEachKeyer([&scene_name, &ctx](DownstreamKeyer *w) { w->SceneChanged(scene_name, ctx); });
	// Keyer Off at Scene Change is one-shot
	if (offAtSceneChange && !holdKeyers)
		SetOffAtSceneChange(false);
	UpdateTabTally();
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
		if (w) {
			w->SetTransition(name.c_str(), transition_type);
		}
	};

	// no transition of its own: the main Transition follows the OBS scene transition, Show / Hide follow Transition
	auto a = tm->addAction(
		QT_UTF8(obs_module_text(transition_type == transitionType::match ? "SameAsSceneTransition" : "SameAsTransition")));
	a->setCheckable(true);
	a->setChecked(transition.empty());
	connect(a, &QAction::triggered, [setTransition] { return setTransition(""); });
	tm->addSeparator();
	obs_frontend_source_list transitions = {};
	get_transitions(get_transitions_data, &transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		const char *n = obs_source_get_name(transitions.sources.array[i]);
		if (!n) {
			continue;
		}
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
		if (w) {
			w->SetTransitionDuration(duration, transition_type);
		}
	};
	connect(duration, (void (QSpinBox::*)(int))&QSpinBox::valueChanged, setDuration);

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

void DownstreamKeyerDock::AddLoadSceneMenu(QMenu *tm)
{
	const auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
	if (!w)
		return;

	auto setLoadScene = [this](std::string name, bool add) {
		const auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget());
		if (!w)
			return;
		if (add)
			w->AddLoadScene(name.c_str());
		else
			w->RemoveLoadScene(name.c_str());
	};

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const std::string name = obs_source_get_name(scenes.sources.array[i]);
		const bool checked = w->IsLoadScene(name.c_str());
		auto a = tm->addAction(QT_UTF8(name.c_str()));
		a->setCheckable(true);
		a->setChecked(checked);
		connect(a, &QAction::triggered, [setLoadScene, name, checked] { setLoadScene(name, !checked); });
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
	tm = popup.addMenu(QT_UTF8(obs_module_text("LoadOnScene")));
	AddLoadSceneMenu(tm);

	if (const auto keyer = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget())) {
		a = popup.addAction(QT_UTF8(obs_module_text("IncludeAudio")));
		a->setCheckable(true);
		const bool included = keyer->GetIncludeAudio();
		a->setChecked(included);
		connect(a, &QAction::triggered, [this, included] {
			if (auto w = dynamic_cast<DownstreamKeyer *>(tabs->currentWidget()))
				w->SetIncludeAudio(!included);
		});
	}

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
		if (w) {
			w->SetHideAfter(duration);
		}
	};
	connect(duration, (void (QSpinBox::*)(int))&QSpinBox::valueChanged, setDuration);
	QWidgetAction *durationAction = new QWidgetAction(tm);
	durationAction->setDefaultWidget(duration);

	tm->addAction(durationAction);
	popup.exec(QCursor::pos());
}

// a keyer name is taken when another keyer has it, ignoring case (websocket requests find keyers by name)
bool DownstreamKeyerDock::KeyerNameExists(const QString &name, int exceptIndex)
{
	for (int i = 0; i < tabs->count(); i++) {
		if (i == exceptIndex)
			continue;
		if (tabs->tabText(i).compare(name, Qt::CaseInsensitive) == 0)
			return true;
		auto w = tabs->widget(i);
		if (w && w->objectName().compare(name, Qt::CaseInsensitive) == 0)
			return true;
	}
	return false;
}

// the default name, numbered past the keyers that already exist: DSK 1, DSK 2, ...
QString DownstreamKeyerDock::UniqueKeyerName(const QString &base)
{
	if (!KeyerNameExists(base))
		return base;
	static const QRegularExpression trailingNumber(QStringLiteral("^(.*?)\\s*(\\d+)$"));
	const auto match = trailingNumber.match(base);
	const QString stem = match.hasMatch() ? match.captured(1) : base;
	for (int n = match.hasMatch() ? match.captured(2).toInt() + 1 : 2;; n++) {
		const QString name = QString("%1 %2").arg(stem).arg(n);
		if (!KeyerNameExists(name))
			return name;
	}
}

// asks for a keyer name until it is not taken by another keyer
bool DownstreamKeyerDock::AskForKeyerName(std::string &name, int exceptIndex)
{
	for (;;) {
		if (!NameDialog::AskForName(this, name))
			return false;
		if (!KeyerNameExists(QT_UTF8(name.c_str()), exceptIndex))
			return true;
		QMessageBox::warning(this, QT_UTF8(obs_module_text("DownstreamKeyer")),
				     QT_UTF8(obs_module_text("KeyerNameExists")));
	}
}

void DownstreamKeyerDock::Add(QString name)
{
	if (name.isEmpty()) {
		std::string std_name = QT_TO_UTF8(UniqueKeyerName(QT_UTF8(obs_module_text("DefaultName"))));
		if (!AskForKeyerName(std_name)) {
			return;
		}
		name = QString::fromUtf8(std_name.c_str());
	}
	if (outputChannel < 7 || outputChannel >= MAX_CHANNELS) {
		outputChannel = 7;
	}
	obs_canvas_t *c = obs_weak_canvas_get_canvas(canvas);
	auto keyer = new DownstreamKeyer(outputChannel + tabs->count(), name, view, c, get_transitions, get_transitions_data);
	obs_canvas_release(c);
	SetupKeyer(keyer);
	tabs->addTab(keyer, keyer->objectName());
}

void DownstreamKeyerDock::Rename()
{
	int i = tabs->currentIndex();
	if (i < 0) {
		return;
	}
	std::string name = QT_TO_UTF8(tabs->tabText(i));
	if (AskForKeyerName(name, i)) {
		tabs->setTabText(i, QT_UTF8(name.c_str()));
		SyncTabButtons();
	}
}

void DownstreamKeyerDock::Remove(int index)
{
	if (index < 0) {
		index = tabs->currentIndex();
	}
	if (index < 0) {
		return;
	}
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

bool DownstreamKeyerDock::SetLoadScene(QString dskName, const char *sceneName, bool add)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (w->objectName() == dskName) {
			if (add)
				w->AddLoadScene(sceneName);
			else
				w->RemoveLoadScene(sceneName);
			return true;
		}
	}
	return false;
}

void DownstreamKeyerDock::get_downstream_keyers(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		return;
	}
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
	if (dsk->KeyerNameExists(dskName)) {
		obs_data_set_string(response_data, "error", "'dsk_name' exists");
		obs_data_set_bool(response_data, "success", false);
		return;
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

// Tie was replaced by the All Keyers buttons (Keyer Off at Scene Change, Hold Keyers)
void DownstreamKeyerDock::set_tie(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(request_data);
	UNUSED_PARAMETER(param);
	obs_data_set_string(response_data, "error", "tie is no longer supported");
	obs_data_set_bool(response_data, "success", false);
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

void DownstreamKeyerDock::add_load_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	load_scene_request(request_data, response_data, true);
}

void DownstreamKeyerDock::remove_load_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	load_scene_request(request_data, response_data, false);
}

void DownstreamKeyerDock::load_scene_request(obs_data_t *request_data, obs_data_t *response_data, bool add)
{
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
	obs_data_set_bool(response_data, "success", dsk->SetLoadScene(QString::fromUtf8(dsk_name), scene_name, add));
}

// websocket dsk_go_live / dsk_go_to_preview; scenes (when not empty) are checked first
bool DownstreamKeyerDock::KeyerAction(QString dskName, QStringList scenes, bool preview, const char **error)
{
	const int count = tabs->count();
	for (int i = 0; i < count; i++) {
		auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i));
		if (!w || w->objectName() != dskName)
			continue;
		if (!w->HasScenes(scenes)) {
			*error = "a scene is not in the downstream keyer";
			return false;
		}
		QMetaObject::invokeMethod(
			w,
			[w, scenes, preview]() {
				if (!scenes.isEmpty() && !w->ArmScenes(scenes))
					return;
				if (!preview)
					w->GoLive();
				else if (!w->IsPreviewing())
					w->TogglePreview();
			},
			Qt::QueuedConnection);
		return true;
	}
	*error = "No downstream keyer with that name found";
	return false;
}

// the dock for the view_name of a request, or nullptr with the error set in the response
static DownstreamKeyerDock *request_dock(obs_data_t *request_data, obs_data_t *response_data)
{
	const char *viewName = obs_data_get_string(request_data, "view_name");
	if (_dsks.find(viewName) == _dsks.end()) {
		obs_data_set_string(response_data, "error", "'view_name' not found");
		obs_data_set_bool(response_data, "success", false);
		return nullptr;
	}
	return _dsks[viewName];
}

void DownstreamKeyerDock::keyer_action_request(obs_data_t *request_data, obs_data_t *response_data, bool preview)
{
	auto dsk = request_dock(request_data, response_data);
	if (!dsk)
		return;
	const char *dsk_name = obs_data_get_string(request_data, "dsk_name");
	if (!dsk_name || !strlen(dsk_name)) {
		obs_data_set_string(response_data, "error", "'dsk_name' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	// optional: check these scenes first, from "scenes" (an array of { "name": ... }) or "scene"
	QStringList scenes;
	obs_data_array_t *array = obs_data_get_array(request_data, "scenes");
	if (array) {
		const size_t count = obs_data_array_count(array);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(array, i);
			const char *name = obs_data_get_string(item, "name");
			if (name && *name)
				scenes.append(QString::fromUtf8(name));
			obs_data_release(item);
		}
		obs_data_array_release(array);
	} else {
		const char *scene_name = obs_data_get_string(request_data, "scene");
		if (scene_name && *scene_name)
			scenes.append(QString::fromUtf8(scene_name));
	}
	const char *error = nullptr;
	const bool success = dsk->KeyerAction(QString::fromUtf8(dsk_name), scenes, preview, &error);
	if (!success && error)
		obs_data_set_string(response_data, "error", error);
	obs_data_set_bool(response_data, "success", success);
}

void DownstreamKeyerDock::go_live(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	keyer_action_request(request_data, response_data, false);
}

void DownstreamKeyerDock::go_to_preview(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	keyer_action_request(request_data, response_data, true);
}

// websocket dsk_all_keyers_off: All Keyers Off
void DownstreamKeyerDock::all_keyers_off(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	auto dsk = request_dock(request_data, response_data);
	if (!dsk)
		return;
	QMetaObject::invokeMethod(dsk, [dsk]() { dsk->AllKeyersOff(); }, Qt::QueuedConnection);
	obs_data_set_bool(response_data, "success", true);
}

// websocket dsk_set_hold_keyers: Hold Keyers on (hold: true) or off
void DownstreamKeyerDock::set_hold_keyers(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	auto dsk = request_dock(request_data, response_data);
	if (!dsk)
		return;
	if (!obs_data_has_user_value(request_data, "hold")) {
		obs_data_set_string(response_data, "error", "'hold' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	const bool hold = obs_data_get_bool(request_data, "hold");
	QMetaObject::invokeMethod(dsk, [dsk, hold]() { dsk->SetHoldKeyers(hold); }, Qt::QueuedConnection);
	obs_data_set_bool(response_data, "success", true);
}

// websocket dsk_set_keyer_off_at_scene_change: arm (enabled: true) or disarm Keyer Off at Scene Change
void DownstreamKeyerDock::set_keyer_off_at_scene_change(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	auto dsk = request_dock(request_data, response_data);
	if (!dsk)
		return;
	if (!obs_data_has_user_value(request_data, "enabled")) {
		obs_data_set_string(response_data, "error", "'enabled' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	const bool enabled = obs_data_get_bool(request_data, "enabled");
	QMetaObject::invokeMethod(dsk, [dsk, enabled]() { dsk->SetOffAtSceneChange(enabled); }, Qt::QueuedConnection);
	obs_data_set_bool(response_data, "success", true);
}

// websocket dsk_transition_keyers: Transition Keyers (main output, studio mode)
void DownstreamKeyerDock::transition_keyers(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	auto dsk = request_dock(request_data, response_data);
	if (!dsk)
		return;
	if (dsk->view || dsk->canvas || !obs_frontend_preview_program_mode_active()) {
		obs_data_set_string(response_data, "error", "studio mode is not active for this output");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	QMetaObject::invokeMethod(dsk, [dsk]() { dsk->TransitionKeyers(); }, Qt::QueuedConnection);
	obs_data_set_bool(response_data, "success", true);
}

void DownstreamKeyerDock::remove_canvas(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(cd);
	auto dock = static_cast<DownstreamKeyerDock *>(data);
	dock->closing = true;
	dock->ClearKeyers();
	dock->deleteLater();
}
