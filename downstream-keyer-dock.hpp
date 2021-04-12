#pragma once
#include <QDockWidget>
#include <QVBoxLayout>
#include <../UI/obs-frontend-api/obs-frontend-api.h>

class DownstreamKeyerDock : public QDockWidget {
	Q_OBJECT
private:
	QVBoxLayout *mainLayout;
	int outputChannel;

private slots:

public:
	DownstreamKeyerDock(QWidget *parent = nullptr);
	~DownstreamKeyerDock();

	void Save(obs_data_t *data);
	void Load(obs_data_t *data);
	void ClearKeyers();
	void AddDefaultKeyer();

	bool loadedBeforeSwitchSceneCollection;
};
