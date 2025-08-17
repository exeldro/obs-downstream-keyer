#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QWidget>
#include <set>

#include "obs.h"
#include "obs-websocket-api.h"

#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(31, 1, 0)
extern "C" {
struct obs_canvas;
typedef struct obs_canvas obs_canvas_t;
#define OBS_FRONTEND_EVENT_CANVAS_ADDED 41
#define OBS_FRONTEND_EVENT_CANVAS_REMOVED 42
extern obs_source_t *(*obs_canvas_get_channel)(obs_canvas_t *canvas, uint32_t channel);
extern void (*obs_canvas_set_channel)(obs_canvas_t *canvas, uint32_t channel, obs_source_t *source);
extern obs_source_t *(*obs_canvas_get_source_by_name)(obs_canvas_t *canvas, const char *name);
extern obs_canvas_t *(*obs_get_main_canvas)(void);
extern void (*obs_canvas_release)(obs_canvas_t *canvas);
extern void (*obs_enum_canvases)(bool (*enum_proc)(void *, obs_canvas_t *), void *param);
extern const char *(*obs_canvas_get_name)(obs_canvas_t *canvas);
}
#endif

typedef void (*get_transitions_callback_t)(void *data, struct obs_frontend_source_list *sources);

class LockedCheckBox : public QCheckBox {
	Q_OBJECT

public:
	LockedCheckBox();
	explicit LockedCheckBox(QWidget *parent);
};

enum transitionType { match, show, hide, override };

class DownstreamKeyer : public QWidget {
	Q_OBJECT

private:
	QTimer hideTimer;
	int outputChannel;
	obs_source_t *transition;
	obs_source_t *showTransition;
	obs_source_t *hideTransition;
	obs_source_t *overrideTransition;
	QListWidget *scenesList;
	QToolBar *scenesToolbar;
	uint32_t transitionDuration;
	uint32_t showTransitionDuration;
	uint32_t hideTransitionDuration;
	uint32_t overrideTransitionDuration;
	uint32_t hideAfter;
	LockedCheckBox *tie;
	obs_hotkey_id null_hotkey_id;
	obs_hotkey_pair_id tie_hotkey_id;
	std::set<std::string> exclude_scenes;
	obs_view_t *view = nullptr;
	obs_canvas_t *canvas = nullptr;
	get_transitions_callback_t get_transitions = nullptr;
	void *get_transitions_data = nullptr;

	static void source_rename(void *data, calldata_t *calldata);
	static void source_remove(void *data, calldata_t *calldata);
	static bool enable_DSK_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);
	static bool disable_DSK_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);

	static void null_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	static bool enable_tie_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);
	static bool disable_tie_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);

	void ChangeSceneIndex(bool relative, int idx, int invalidIdx);

private slots:
	void on_actionAddScene_triggered();
	void on_actionRemoveScene_triggered();
	void on_actionSceneUp_triggered();
	void on_actionSceneDown_triggered();
	void on_actionSceneNull_triggered();
	void apply_source(obs_source_t *newSource);
	void apply_selected_source();
	void on_scenesList_itemSelectionChanged();
signals:

public:
	DownstreamKeyer(int channel, QString name, obs_view_t *view = nullptr, obs_canvas_t *canvas = nullptr,
			get_transitions_callback_t get_transitions = nullptr, void *get_transitions_data = nullptr);
	~DownstreamKeyer();

	void Save(obs_data_t *data);
	void Load(obs_data_t *data);
	void SetTransition(const char *transition_name, enum transitionType transition_type = match);
	std::string GetTransition(enum transitionType transition_type = match);
	void SetTransitionDuration(int duration, enum transitionType transition_type = match);
	int GetTransitionDuration(enum transitionType transition_type = match);
	void SetHideAfter(int duration);
	int GetHideAfter();
	void SceneChanged(std::string scene);
	void AddExcludeScene(const char *scene_name);
	void RemoveExcludeScene(const char *scene_name);
	bool IsSceneExcluded(const char *scene_name);
	QString GetScene();
	bool SwitchToScene(QString scene_name);
	void add_scene(QString scene_name, obs_source_t *s, int insertBeforeRow);
	bool AddScene(QString scene_name, int insertBeforeRow);
	bool RemoveScene(QString scene_name);
	void SetTie(bool tie);
	void SetOutputChannel(int outputChannel);
};
