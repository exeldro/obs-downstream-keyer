#pragma once

#include <QCheckBox>
#include <qcombobox.h>
#include <qlabel.h>
#include <qlistwidget.h>
#include <qspinbox.h>
#include <qtoolbar.h>
#include <QWidget>
#include <set>

#include "obs-websocket-api.h"
#include "obs.h"

class LockedCheckBox : public QCheckBox {
	Q_OBJECT

public:
	LockedCheckBox();
	explicit LockedCheckBox(QWidget *parent);
};

enum transitionType { match, show, hide };

class DownstreamKeyer : public QWidget {
	Q_OBJECT

private:
	int outputChannel;
	obs_source_t *transition;
	obs_source_t *showTransition;
	obs_source_t *hideTransition;
	QListWidget *scenesList;
	QToolBar *scenesToolbar;
	uint32_t transitionDuration;
	uint32_t showTransitionDuration;
	uint32_t hideTransitionDuration;
	LockedCheckBox *tie;
	obs_hotkey_id null_hotkey_id;
	obs_hotkey_pair_id tie_hotkey_id;
	std::set<std::string> exclude_scenes;
	obs_websocket_vendor vendor;

	static void source_rename(void *data, calldata_t *calldata);
	static void source_remove(void *data, calldata_t *calldata);
	static bool enable_DSK_hotkey(void *data, obs_hotkey_pair_id id,
				      obs_hotkey_t *hotkey, bool pressed);
	static bool disable_DSK_hotkey(void *data, obs_hotkey_pair_id id,
				       obs_hotkey_t *hotkey, bool pressed);

	static void null_hotkey(void *data, obs_hotkey_id id,
				obs_hotkey_t *hotkey, bool pressed);

	static bool enable_tie_hotkey(void *data, obs_hotkey_pair_id id,
				      obs_hotkey_t *hotkey, bool pressed);
	static bool disable_tie_hotkey(void *data, obs_hotkey_pair_id id,
				       obs_hotkey_t *hotkey, bool pressed);

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
	DownstreamKeyer(int channel, QString name, obs_websocket_vendor vendor);
	~DownstreamKeyer();

	void Save(obs_data_t *data);
	void Load(obs_data_t *data);
	void SetTransition(const char *transition_name,
			   enum transitionType transition_type = match);
	std::string GetTransition(enum transitionType transition_type = match);
	void SetTransitionDuration(int duration,
				   enum transitionType transition_type = match);
	int GetTransitionDuration(enum transitionType transition_type = match);
	void SceneChanged(std::string scene);
	void AddExcludeScene(const char *scene_name);
	void RemoveExcludeScene(const char *scene_name);
	bool IsSceneExcluded(const char *scene_name);
	bool SwitchToScene(QString scene_name);
	void add_scene(QString scene_name, obs_source_t *s);
	bool AddScene(QString scene_name);
	bool RemoveScene(QString scene_name);
	void SetTie(bool tie);
};
