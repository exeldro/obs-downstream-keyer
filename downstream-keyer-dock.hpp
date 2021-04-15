#pragma once
#include <QDockWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <../UI/obs-frontend-api/obs-frontend-api.h>

class DownstreamKeyerDock : public QDockWidget {
	Q_OBJECT
private:
	QTabWidget *tabs;
	int outputChannel;
	bool loadedBeforeSwitchSceneCollection;

	static void frontend_event(enum obs_frontend_event event, void *data);
	static void frontend_save_load(obs_data_t *save_data, bool saving,
				       void *data);

	void Save(obs_data_t *data);
	void Load(obs_data_t *data);
	void ClearKeyers();
	void AddDefaultKeyer();
	void ConfigClicked();
private slots:
	void Add();
	void Rename();
	void Remove();

public:
	DownstreamKeyerDock(QWidget *parent = nullptr);
	~DownstreamKeyerDock();
};
