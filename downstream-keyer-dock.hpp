#pragma once
#include <QDockWidget>
#include <qmenu.h>
#include <QTabWidget>
#include <QVBoxLayout>
#include <obs-frontend-api.h>
#include "downstream-keyer.hpp"
#include "obs-websocket-api.h"

class DownstreamKeyerDock : public QDockWidget {
	Q_OBJECT
private:
	QTabWidget *tabs;
	int outputChannel;
	bool loaded;
	obs_websocket_vendor vendor = nullptr;

	static void frontend_event(enum obs_frontend_event event, void *data);
	static void change_scene(obs_data_t *request_data,
				 obs_data_t *response_data, void *param);
	static void get_downstream_keyers(obs_data_t *request_data,
					  obs_data_t *response_data,
					  void *param);
	static void frontend_save_load(obs_data_t *save_data, bool saving,
				       void *data);

	void Save(obs_data_t *data);
	void Load(obs_data_t *data);
	bool SwitchDSK(QString dskName, QString sceneName);
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
	DownstreamKeyerDock(QWidget *parent = nullptr);
	~DownstreamKeyerDock();
	void RegisterObsWebsocket();
};
