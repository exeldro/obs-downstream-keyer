#pragma once
#include <QDockWidget>
#include <qmenu.h>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFrame>
#include <vector>
#include <QPushButton>
#include <obs-frontend-api.h>
#include "downstream-keyer.hpp"
#include "obs-websocket-api.h"

class DownstreamKeyerDock : public QFrame {
	Q_OBJECT
private:
	QTabWidget *tabs;
	int outputChannel;
	bool loaded = false;
	bool closing = false;
	obs_view_t *view = nullptr;
	obs_weak_canvas_t *canvas = nullptr;
	std::string viewName;
	get_transitions_callback_t get_transitions = nullptr;
	void *get_transitions_data = nullptr;

	// All Keyers: Hold Keyers overrides Keyer Off at Scene Change, the Keyer Off button overrides both
	bool holdKeyers = false;
	bool offAtSceneChange = false;
	// Keyer Off at Scene Change is one-shot: it was used by the program transition that is running
	bool offAtSceneChangeUsed = false;
	QPushButton *offAtSceneChangeButton = nullptr;
	QPushButton *holdKeyersButton = nullptr;
	QPushButton *transitionKeyersButton = nullptr;

	void Save(obs_data_t *data);
	void Load(obs_data_t *data);
	QString GetScene(QString dskName);
	bool SwitchDSK(QString dskName, QString sceneName);
	bool AddScene(QString dskName, QString sceneName, int insertBeforeRow);
	bool RemoveScene(QString dskName, QString sceneName);
	bool SetTransition(const QString &chars, const char *transition, int duration, transitionType tt);
	bool AddExcludeScene(QString dskName, const char *sceneName);
	bool RemoveExcludeScene(QString dskName, const char *sceneName);
	bool SetLoadScene(QString dskName, const char *sceneName, bool add);
	static void load_scene_request(obs_data_t *request_data, obs_data_t *response_data, bool add);

	template<typename F> void ForEachKeyer(F f)
	{
		if (closing)
			return;
		for (int i = 0; i < tabs->count(); i++) {
			if (auto w = dynamic_cast<DownstreamKeyer *>(tabs->widget(i)))
				f(w);
		}
	}
	// program scene whose transition start was already handled, so the end of it is skipped
	std::string transitionStartedScene;
	static void transition_start(void *data, calldata_t *cd);
	// the OBS scene transitions are private sources, so their own transition_start signals are connected
	std::vector<obs_weak_source_t *> hookedTransitions;
	void HookTransitions();
	void UnhookTransitions();
	void ProgramTransitionStarting(obs_source_t *dest);
	bool KeyerAction(QString dskName, QStringList scenes, bool preview, const char **error);
	static void keyer_action_request(obs_data_t *request_data, obs_data_t *response_data, bool preview);

	SceneChangeContext GetSceneChangeContext(const std::string &scene, bool forPreview);
	std::string ProgramSceneName();
	void SetupKeyer(DownstreamKeyer *keyer);
	void AllKeyersOff();
	// Transition Keyers (studio mode): the program keyers become what the preview shows, without a scene transition
	void TransitionKeyers();
	void SetOffAtSceneChange(bool on);
	void SetHoldKeyers(bool on);
	void UpdateAllKeyersButtons();

	void ClearKeyers();
	void AddDefaultKeyer();
	void ConfigClicked();
	void AddTransitionMenu(QMenu *tm, enum transitionType transition_type);
	void AddExcludeSceneMenu(QMenu *tm);
	void AddLoadSceneMenu(QMenu *tm);
private slots:
	void SceneChanged();
	void Add(QString name = "");
	void Rename();
	void Remove(int index = -1);

public:
	DownstreamKeyerDock(QWidget *parent = nullptr, int outputChannel = 7, obs_view_t *view = nullptr,
			    obs_canvas_t *canvas = nullptr, const char *view_name = nullptr);
	~DownstreamKeyerDock();

	void RefreshPreviewOverlays(bool force = true);
	void UpdateTabTally();

	void SetTransitions(get_transitions_callback_t get_transitions = nullptr, void *get_transitions_data = nullptr);

	inline obs_view_t *GetView() { return view; }
	inline obs_canvas_t *GetCanvas() { return obs_weak_canvas_get_canvas(canvas); }

	static void frontend_event(enum obs_frontend_event event, void *data);
	static void frontend_save_load(obs_data_t *save_data, bool saving, void *data);

	static void get_downstream_keyers(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void get_downstream_keyer(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void add_downstream_keyer(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void remove_downstream_keyer(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void get_scene(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void change_scene(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void add_scene(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void remove_scene(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void set_tie(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void set_transition(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void add_exclude_scene(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void remove_exclude_scene(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void add_load_scene(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void remove_load_scene(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void go_live(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void go_to_preview(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void all_keyers_off(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void set_hold_keyers(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void set_keyer_off_at_scene_change(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void transition_keyers(obs_data_t *request_data, obs_data_t *response_data, void *param);
	static void remove_canvas(void *data, calldata_t *cd);
};
