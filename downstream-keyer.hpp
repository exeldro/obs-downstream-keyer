#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QWidget>
#include <set>

#include "obs.h"
#include "obs-websocket-api.h"

typedef void (*get_transitions_callback_t)(void *data, struct obs_frontend_source_list *sources);

class LockedCheckBox : public QCheckBox {
	Q_OBJECT

public:
	LockedCheckBox();
	explicit LockedCheckBox(QWidget *parent);
};

enum transitionType { match, show, hide, override };

// what the keyer does when the program scene transitions
enum sceneChangeMode {
	sceneChangeNone = 0, // neither button on: the live scenes stay live
	sceneChangeOn = 1,   // Go Live at Scene Change: the checked scenes go live with the transition
	sceneChangeOff = 2,  // Keyer Off at Scene Change: the keyer goes off with the transition
};

// Registers the private video only source used to leave keyer audio out of the mix.
void dsk_register_video_only_source();

// Remove the scene items Go to Preview added to a scene. owner nullptr removes them for every keyer.
void dsk_remove_preview_items(obs_source_t *scene_source, const char *owner);
// Remove leftover Go to Preview scene items from all scenes (for example after a crash while previewing).
void dsk_purge_preview_items();

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
	// side column
	QWidget *sideColumn;
	QPushButton *goLiveButton;
	QPushButton *previewButton;
	QPushButton *keyerOffButton;
	QPushButton *changeOnButton;
	QPushButton *changeOffButton;
	uint32_t transitionDuration;
	uint32_t showTransitionDuration;
	uint32_t hideTransitionDuration;
	uint32_t overrideTransitionDuration = 300;
	uint32_t hideAfter = 0;
	int sceneChangeMode = sceneChangeNone;
	// mix the audio of the keyer scenes into the output (off by default: video only)
	bool includeAudio = false;
	obs_hotkey_id null_hotkey_id;
	obs_hotkey_pair_id tie_hotkey_id;
	obs_hotkey_id go_live_hotkey_id;
	obs_hotkey_id preview_hotkey_id;

	// Keyer scenes on the output, in list order (kept while an excluded program scene hides the output).
	// liveSource is what goes on the output channel: the scene itself, or a private scene layering several.
	QStringList liveScenes;
	obs_source_t *liveSource = nullptr;
	// last live scenes, loaded again when a Load On Scene program scene goes live
	QStringList lastLiveScenes;
	// checked scenes, to notice when the check boxes change
	QStringList checkedCache;

	// Go to Preview (studio mode, main canvas only): previewSource is added on top of the preview scene
	QStringList previewScenes;
	obs_source_t *previewSource = nullptr;
	obs_weak_source_t *previewTarget = nullptr;
	obs_weak_source_t *previewProgramAtAttach = nullptr;
	std::string previewToken;

	std::set<std::string> exclude_scenes;
	// program scene names the keyer output is shown on (Load On Scene)
	// when non-empty, the keyer output is only on while one of these program scenes is live
	std::set<std::string> load_scenes;
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

	static void go_live_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static void preview_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	void ChangeSceneIndex(bool relative, int idx, int invalidIdx);
	std::string GetProgramSceneName();
	obs_source_t *GetSourceByName(const QString &name);
	obs_source_t *BuildSetSource(const QStringList &scenes);
	QStringList CheckedScenes();
	QStringList InListOrder(const QStringList &scenes);
	void SetLiveScenes(const QStringList &scenes, obs_source_t *source = nullptr, bool useTransition = true);
	void SetSceneHotkey(obs_hotkey_pair_id id, bool show);
	bool IsHiddenOn(const std::string &scene);
	bool IsPreviewTakeTo(const std::string &scene);
	void set_output(obs_source_t *source);
	void UpdateTally();
	void UpdateButtons();
	void AttachPreview();
	void RemovePreviewItems(bool everywhere);
	bool PreviewSupported();
	void ScenesChecked();

private slots:
	void on_actionAddScene_triggered();
	void on_actionRemoveScene_triggered();
	void on_actionSceneUp_triggered();
	void on_actionSceneDown_triggered();
	void on_actionSceneNull_triggered();
	void apply_source(obs_source_t *newSource, bool useTransition = true);
	void apply_live_source();

public slots:
	void GoLive();
	void TogglePreview();
	void KeyerOff();
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
	// the program scene changes to scene (main output: when its transition starts)
	void SceneChanged(std::string scene);
	void AddExcludeScene(const char *scene_name);
	void RemoveExcludeScene(const char *scene_name);
	bool IsSceneExcluded(const char *scene_name);
	void AddLoadScene(const char *scene_name);
	void RemoveLoadScene(const char *scene_name);
	bool IsLoadScene(const char *scene_name);
	QString GetScene();
	bool SwitchToScene(QString scene_name);
	void add_scene(QString scene_name, obs_source_t *s, int insertBeforeRow);
	bool AddScene(QString scene_name, int insertBeforeRow);
	bool RemoveScene(QString scene_name);
	void SetTie(bool tie);
	void SetSceneChangeMode(int mode);
	void SetIncludeAudio(bool include);
	bool GetIncludeAudio() { return includeAudio; }
	void SetOutputChannel(int outputChannel);
	bool IsPreviewing() { return !previewScenes.isEmpty(); }
	bool ArmScene(const QString &scene_name);
	void StudioModeChanged();
	void CheckPreviewTake();
	void PreviewSceneChanged();
	void ClearPreview();
};
