#pragma once
#include <QDockWidget>
#include <qmenu.h>
#include <QTabWidget>
#include <QVBoxLayout>
#include <obs-frontend-api.h>
#include "downstream-keyer.hpp"
#include "obs-websocket-api.h"

class DownstreamKeyerDock : public QWidget {
	Q_OBJECT
private:
	QTabWidget *tabs;
	int outputChannel;
	bool loaded;
	obs_view_t *view = nullptr;
	std::string viewName;
	get_transitions_callback_t get_transitions = nullptr;
	void *get_transitions_data = nullptr;

	static void frontend_event(enum obs_frontend_event event, void *data);
	static void frontend_save_load(obs_data_t *save_data, bool saving,
				       void *data);

	void Save(obs_data_t *data);
	void Load(obs_data_t *data);
	bool SwitchDSK(QString dskName, QString sceneName);
	bool AddScene(QString dskName, QString sceneName);
	bool RemoveScene(QString dskName, QString sceneName);
	bool SetTie(QString dskName, bool tie);
	bool SetTransition(const QString &chars, const char *transition,
			   int duration, transitionType tt);
	bool AddExcludeScene(QString dskName, const char *sceneName);
	bool RemoveExcludeScene(QString dskName, const char *sceneName);

	void ClearKeyers();
	void AddDefaultKeyer();
	void ConfigClicked();
	void SceneChanged();
	void AddTransitionMenu(QMenu *tm, enum transitionType transition_type);
	void AddExcludeSceneMenu(QMenu *tm);
private slots:
	void Add();
	void Rename();
	void Remove();

public:
	DownstreamKeyerDock(QWidget *parent = nullptr,
			    int outputChannel = 7,
			    obs_view_t *view = nullptr,
			    const char *view_name = nullptr,
			    get_transitions_callback_t get_transitions = nullptr,
			    void *get_transitions_data = nullptr);
	~DownstreamKeyerDock();

	inline obs_view_t *GetView() { return view; }

	static void get_downstream_keyers(obs_data_t *request_data,
					  obs_data_t *response_data,
					  void *param);
	static void get_downstream_keyer(obs_data_t *request_data,
					 obs_data_t *response_data,
					 void *param);
	static void change_scene(obs_data_t *request_data,
				 obs_data_t *response_data, void *param);
	static void add_scene(obs_data_t *request_data,
			      obs_data_t *response_data, void *param);
	static void remove_scene(obs_data_t *request_data,
				 obs_data_t *response_data, void *param);
	static void set_tie(obs_data_t *request_data, obs_data_t *response_data,
			    void *param);
	static void set_transition(obs_data_t *request_data,
				   obs_data_t *response_data, void *param);
	static void add_exclude_scene(obs_data_t *request_data,
				      obs_data_t *response_data, void *param);
	static void remove_exclude_scene(obs_data_t *request_data,
					 obs_data_t *response_data,
					 void *param);
};
