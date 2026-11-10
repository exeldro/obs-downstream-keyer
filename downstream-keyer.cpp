#include "downstream-keyer.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QVBoxLayout>
#include <obs-frontend-api.h>

#include "obs-module.h"
#include <algorithm>
#include <atomic>
#include <vector>

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

extern obs_websocket_vendor vendor;

// draws the scene check box on the right side of the scene name
class RightCheckBoxDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

protected:
	void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
	{
		QStyledItemDelegate::initStyleOption(option, index);
		option->direction = Qt::RightToLeft;
		option->displayAlignment = Qt::AlignLeft | Qt::AlignAbsolute | Qt::AlignVCenter;
	}
};

DownstreamKeyer::DownstreamKeyer(int channel, QString name, obs_view_t *v, obs_canvas_t *c, get_transitions_callback_t gt,
				 void *gtd)
	: outputChannel(channel),
	  transition(nullptr),
	  showTransition(nullptr),
	  hideTransition(nullptr),
	  overrideTransition(nullptr),
	  transitionDuration(300),
	  showTransitionDuration(300),
	  hideTransitionDuration(300),
	  view(v),
	  canvas(c),
	  get_transitions(gt),
	  get_transitions_data(gtd)
{
	setObjectName(name);
	auto layout = new QVBoxLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);

	scenesList = new QListWidget(this);
	scenesList->setObjectName(QStringLiteral("scenes"));
	QSizePolicy sizePolicy6(QSizePolicy::Preferred, QSizePolicy::Expanding);
	sizePolicy6.setHorizontalStretch(0);
	sizePolicy6.setVerticalStretch(0);
	sizePolicy6.setHeightForWidth(scenesList->sizePolicy().hasHeightForWidth());
	scenesList->setSizePolicy(sizePolicy6);
	scenesList->setContextMenuPolicy(Qt::CustomContextMenu);
	scenesList->setFrameShape(QFrame::NoFrame);
	scenesList->setFrameShadow(QFrame::Plain);
	scenesList->setProperty("showDropIndicator", QVariant(true));
	scenesList->setDragEnabled(true);
	scenesList->setDragDropMode(QAbstractItemView::InternalMove);
	scenesList->setDefaultDropAction(Qt::TargetMoveAction);
	scenesList->setItemDelegate(new RightCheckBoxDelegate(scenesList));
	connect(scenesList, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { ScenesChecked(); });
	connect(scenesList->model(), &QAbstractItemModel::rowsMoved, this, [this]() { ScenesChecked(); });

	previewToken = QString::number(reinterpret_cast<quintptr>(this), 16).toStdString();

	auto row = new QHBoxLayout;
	row->setSpacing(0);
	row->setContentsMargins(0, 0, 0, 0);
	row->addWidget(scenesList);

	sideColumn = new QWidget(this);
	// keep the buttons as narrow as their longest label: the scenes list takes any extra width
	sideColumn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	auto column = new QVBoxLayout(sideColumn);
	column->setContentsMargins(4, 4, 4, 4);
	column->setSpacing(4);

	auto addTitle = [&](const char *text) {
		auto title = new QLabel(QT_UTF8(obs_module_text(text)), sideColumn);
		title->setAlignment(Qt::AlignCenter);
		// wraps to the width of the buttons instead of making the column wider
		title->setWordWrap(true);
		title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		QFont font = title->font();
		font.setBold(true);
		title->setFont(font);
		column->addWidget(title);
	};

	addTitle("KeyerActionTitle");

	goLiveButton = new QPushButton(QT_UTF8(obs_module_text("GoLive")), sideColumn);
	goLiveButton->setObjectName(QStringLiteral("dskGoLive"));
	connect(goLiveButton, &QPushButton::clicked, this, &DownstreamKeyer::GoLive);
	column->addWidget(goLiveButton);

	previewButton = new QPushButton(QT_UTF8(obs_module_text("GoToPreview")), sideColumn);
	previewButton->setObjectName(QStringLiteral("dskGoToPreview"));
	previewButton->setCheckable(true);
	connect(previewButton, &QPushButton::clicked, this, &DownstreamKeyer::TogglePreview);
	column->addWidget(previewButton);

	transitionKeyersButton = new QPushButton(QT_UTF8(obs_module_text("TransitionKeyer")), sideColumn);
	transitionKeyersButton->setObjectName(QStringLiteral("dskTransitionKeyer"));
	transitionKeyersButton->setToolTip(QT_UTF8(obs_module_text("TransitionKeyerTip")));
	transitionKeyersButton->setEnabled(false);
	// Go to Program: this keyer's cue, or else its checked scenes, go live without a scene transition and
	// without touching other keyers (the same as Go Live)
	connect(transitionKeyersButton, &QPushButton::clicked, this, &DownstreamKeyer::GoLive);
	column->addWidget(transitionKeyersButton);

	keyerOffButton = new QPushButton(QT_UTF8(obs_module_text("KeyerOff")), sideColumn);
	keyerOffButton->setObjectName(QStringLiteral("dskKeyerOff"));
	keyerOffButton->setToolTip(QT_UTF8(obs_module_text("KeyerOffTip")));
	connect(keyerOffButton, &QPushButton::clicked, this, &DownstreamKeyer::KeyerOff);
	column->addWidget(keyerOffButton);

	column->addStretch();
	row->addWidget(sideColumn);

	layout->addLayout(row);

	scenesToolbar = new QToolBar(this);
	scenesToolbar->setObjectName(QStringLiteral("scenesToolbar"));
	scenesToolbar->setIconSize(QSize(16, 16));
	scenesToolbar->setFloatable(false);

	auto actionAddScene = new QAction(this);
	actionAddScene->setObjectName(QStringLiteral("actionAddScene"));
	actionAddScene->setProperty("themeID", "addIconSmall");
	actionAddScene->setProperty("class", "icon-plus");
	actionAddScene->setText(QT_UTF8(obs_module_text("Add")));
	connect(actionAddScene, SIGNAL(triggered()), this, SLOT(on_actionAddScene_triggered()));
	scenesToolbar->addAction(actionAddScene);

	auto actionRemoveScene = new QAction(this);
	actionRemoveScene->setObjectName(QStringLiteral("actionRemoveScene"));
	actionRemoveScene->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	actionRemoveScene->setProperty("themeID", "removeIconSmall");
	actionRemoveScene->setProperty("class", "icon-minus");
	actionRemoveScene->setText(QT_UTF8(obs_module_text("Remove")));
	connect(actionRemoveScene, SIGNAL(triggered()), this, SLOT(on_actionRemoveScene_triggered()));
	scenesToolbar->addAction(actionRemoveScene);

	scenesToolbar->addSeparator();

	auto actionSceneUp = new QAction(this);
	actionSceneUp->setObjectName(QStringLiteral("actionSceneUp"));
	actionSceneUp->setProperty("themeID", "upArrowIconSmall");
	actionSceneUp->setProperty("class", "icon-up");
	actionSceneUp->setText(QT_UTF8(obs_module_text("MoveUp")));
	connect(actionSceneUp, SIGNAL(triggered()), this, SLOT(on_actionSceneUp_triggered()));
	scenesToolbar->addAction(actionSceneUp);

	auto actionSceneDown = new QAction(this);
	actionSceneDown->setObjectName(QStringLiteral("actionSceneDown"));
	actionSceneDown->setProperty("themeID", "downArrowIconSmall");
	actionSceneDown->setProperty("class", "icon-down");
	actionSceneDown->setText(QT_UTF8(obs_module_text("MoveDown")));
	connect(actionSceneDown, SIGNAL(triggered()), this, SLOT(on_actionSceneDown_triggered()));
	scenesToolbar->addAction(actionSceneDown);

	// Themes need the QAction dynamic properties
	for (QAction *x : scenesToolbar->actions()) {
		QWidget *temp = scenesToolbar->widgetForAction(x);

		for (QByteArray &y : x->dynamicPropertyNames()) {
			temp->setProperty(y, x->property(y));
		}
	}

	layout->addWidget(scenesToolbar);

	layout->addItem(new QSpacerItem(150, 0, QSizePolicy::Fixed, QSizePolicy::Minimum));

	const auto sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_rename", source_rename, this);
	signal_handler_connect(sh, "source_remove", source_remove, this);

	setLayout(layout);
	QString disableDskHotkeyName = QT_UTF8(obs_module_text("DisableDSK"));
	disableDskHotkeyName += " ";
	disableDskHotkeyName += name;

	null_hotkey_id =
		obs_hotkey_register_frontend(QT_TO_UTF8(disableDskHotkeyName), QT_TO_UTF8(disableDskHotkeyName), null_hotkey, this);
	QString goLiveHotkeyName = QT_UTF8(obs_module_text("GoLive"));
	goLiveHotkeyName += " ";
	goLiveHotkeyName += name;
	go_live_hotkey_id =
		obs_hotkey_register_frontend(QT_TO_UTF8(goLiveHotkeyName), QT_TO_UTF8(goLiveHotkeyName), go_live_hotkey, this);
	QString previewHotkeyName = QT_UTF8(obs_module_text("GoToPreview"));
	previewHotkeyName += " ";
	previewHotkeyName += name;
	preview_hotkey_id =
		obs_hotkey_register_frontend(QT_TO_UTF8(previewHotkeyName), QT_TO_UTF8(previewHotkeyName), preview_hotkey, this);

	connect(&hideTimer, &QTimer::timeout, [this]() {
		hideTimer.stop();
		KeyerOff();
	});

	UpdateButtons();
}

DownstreamKeyer::~DownstreamKeyer()
{
	if (view) {
		//obs_view_set_source(view, outputChannel, nullptr);
	} else if (canvas) {
		//obs_canvas_set_channel(canvas, outputChannel, nullptr);
	} else {
		obs_set_output_source(outputChannel, nullptr);
	}
	obs_hotkey_unregister(null_hotkey_id);
	obs_hotkey_unregister(go_live_hotkey_id);
	obs_hotkey_unregister(preview_hotkey_id);

	RemoveOverlay(false);
	obs_source_release(previewSource);
	previewSource = nullptr;
	obs_source_release(liveSource);
	liveSource = nullptr;

	if (transition) {
		obs_transition_clear(transition);
		obs_source_release(transition);
		transition = nullptr;
	}
	if (showTransition) {
		obs_transition_clear(showTransition);
		obs_source_release(showTransition);
		showTransition = nullptr;
	}
	if (hideTransition) {
		obs_transition_clear(hideTransition);
		obs_source_release(hideTransition);
		hideTransition = nullptr;
	}
	if (overrideTransition) {
		obs_transition_clear(overrideTransition);
		obs_source_release(overrideTransition);
		overrideTransition = nullptr;
	}
	if (defaultTransition) {
		obs_transition_clear(defaultTransition);
		obs_source_release(defaultTransition);
		defaultTransition = nullptr;
	}
	const auto sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_rename", source_rename, this);
	signal_handler_disconnect(sh, "source_remove", source_remove, this);
	scenesList->blockSignals(true);
	while (scenesList->count()) {
		const auto item = scenesList->item(0);
		scenesList->removeItemWidget(item);
		obs_hotkey_pair_unregister(item->data(Qt::UserRole).toUInt());
		delete item;
	}
	delete scenesList;
	delete scenesToolbar;
}

void DownstreamKeyer::on_actionAddScene_triggered()
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
		obs_source_t *source = obs_canvas_get_channel(canvas, 0);
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
		scene = obs_frontend_preview_program_mode_active() ? obs_frontend_get_current_preview_scene()
								   : obs_frontend_get_current_scene();
	}
	if (!scene)
		return;
	auto sceneName = QT_UTF8(obs_source_get_name(scene));
	if (scenesList->findItems(sceneName, Qt::MatchFixedString).count() == 0) {
		const auto currentRow = scenesList->currentRow();
		add_scene(sceneName, scene, currentRow);
	}

	obs_source_release(scene);
}

void DownstreamKeyer::on_actionRemoveScene_triggered()
{
	auto item = scenesList->currentItem();
	if (!item)
		return;
	scenesList->removeItemWidget(item);
	obs_hotkey_pair_unregister(item->data(Qt::UserRole).toUInt());
	delete item;
	ScenesChecked();
}

void DownstreamKeyer::on_actionSceneUp_triggered()
{
	ChangeSceneIndex(true, -1, 0);
}

void DownstreamKeyer::on_actionSceneDown_triggered()
{
	ChangeSceneIndex(true, 1, scenesList->count() - 1);
}

void DownstreamKeyer::KeyerOff()
{
	// only the output goes off: the checked scenes and any preview stay
	hideTimer.stop();
	liveScenes.clear();
	obs_source_release(liveSource);
	liveSource = nullptr;
	apply_source(nullptr);
	// safety net: never leave this keyer's preview copy in the program scene
	if (PreviewSupported()) {
		obs_source_t *program = dsk_get_main_program_scene();
		if (program)
			dsk_remove_preview_items(program, previewToken.c_str());
		obs_source_release(program);
	}
	UpdateTally();
}

void DownstreamKeyer::set_output(obs_source_t *source)
{
	if (view) {
		obs_view_set_source(view, outputChannel, source);
	} else if (canvas) {
		obs_canvas_set_channel(canvas, outputChannel, source);
	} else {
		obs_set_output_source(outputChannel, source);
	}
}

void DownstreamKeyer::apply_source(obs_source_t *const newSource, bool useTransition)
{
	obs_source_t *retiredTransition = nullptr; // an older copy of the scene transition, released at the end
	if (newSource && hideAfter > 0 && !held) {
		hideTimer.stop();
		hideTimer.setInterval(hideAfter);
		hideTimer.start();
	}
	obs_source_t *prevSource = view     ? obs_view_get_source(view, outputChannel)
				   : canvas ? obs_canvas_get_channel(canvas, outputChannel)
					    : obs_get_output_source(outputChannel);
	obs_source_t *prevTransition = nullptr;
	if (prevSource && obs_source_get_type(prevSource) == OBS_SOURCE_TYPE_TRANSITION) {
		prevTransition = prevSource;
		prevSource = obs_transition_get_active_source(prevSource);
	}
	obs_source_t *newTransition = nullptr;
	uint32_t newTransitionDuration = transitionDuration;
	if (prevSource == newSource) {
	} else if (!prevSource && newSource && showTransition) {
		newTransition = showTransition;
		newTransitionDuration = showTransitionDuration;
	} else if (prevSource && !newSource && hideTransition) {
		newTransition = hideTransition;
		newTransitionDuration = hideTransitionDuration;
	} else {
		auto ph = obs_get_proc_handler();
		calldata_t cd = {0};
		calldata_set_string(&cd, "from_scene", obs_source_get_name(prevSource));
		calldata_set_string(&cd, "to_scene", obs_source_get_name(newSource));
		if (proc_handler_call(ph, "get_transition_table_transition", &cd)) {
			const char *p = calldata_string(&cd, "transition");
			SetTransition(p ? p : "", transitionType::override);
			SetTransitionDuration(calldata_int(&cd, "duration"), transitionType::override);
		} else {
			SetTransition("", transitionType::override);
		}
		calldata_free(&cd);
		if (overrideTransition) {
			newTransition = overrideTransition;
			newTransitionDuration = overrideTransitionDuration;
		} else if (transition) {
			newTransition = transition;
		} else {
			// no keyer Transition set: follow the OBS scene transition and its duration
			newTransition = DefaultTransition(newTransitionDuration, &retiredTransition);
		}
	}
	if (!useTransition)
		newTransition = nullptr;
	if (prevSource == newSource) {
		//skip if nothing changed
	} else {
		if (!newTransition) {
			set_output(newSource);
		} else {
			obs_transition_set(newTransition, prevSource);

			obs_transition_start(newTransition, OBS_TRANSITION_MODE_AUTO, newTransitionDuration, newSource);

			if (prevTransition != newTransition) {
				set_output(newTransition);
			}
		}
		if (vendor) {
			const auto data = obs_data_create();
			obs_data_set_string(data, "dsk_name", QT_TO_UTF8(objectName()));
			obs_data_set_int(data, "dsk_channel", outputChannel);
			obs_data_set_string(data, "new_scene", QT_TO_UTF8(liveScenes.join(", ")));
			obs_data_set_string(data, "old_scene", prevSource ? obs_source_get_name(prevSource) : "");
			obs_websocket_vendor_emit_event(vendor, "dsk_scene_changed", data);
			obs_data_release(data);
		}
	}

	obs_source_release(prevSource);
	obs_source_release(prevTransition);
	if (retiredTransition) {
		obs_transition_clear(retiredTransition);
		obs_source_release(retiredTransition);
	}
}

// The OBS scene transition (Scene Transitions dock) as a private copy for this keyer's output, kept in step with
// it: a new copy when another transition is picked, the settings copied over otherwise. An older copy that may still
// be on the output is handed back in retired, to release once the output uses the new one.
obs_source_t *DownstreamKeyer::DefaultTransition(uint32_t &duration, obs_source_t **retired)
{
	obs_source_t *current = obs_frontend_get_current_transition();
	if (!current)
		return defaultTransition;
	const char *name = obs_source_get_name(current);
	if (!defaultTransition || strcmp(obs_source_get_id(defaultTransition), obs_source_get_id(current)) != 0 ||
	    strcmp(obs_source_get_name(defaultTransition), name ? name : "") != 0) {
		obs_source_t *copy = obs_source_duplicate(current, name, true);
		if (copy) {
			*retired = defaultTransition;
			defaultTransition = copy;
		}
	} else {
		obs_data_t *settings = obs_source_get_settings(current);
		obs_source_update(defaultTransition, settings);
		obs_data_release(settings);
	}
	obs_source_release(current);
	duration = (uint32_t)obs_frontend_get_transition_duration();
	return defaultTransition;
}

void DownstreamKeyer::apply_live_source()
{
	apply_source(liveSource);
}

obs_source_t *DownstreamKeyer::GetSourceByName(const QString &name)
{
	if (name.isEmpty())
		return nullptr;
	return canvas ? obs_canvas_get_source_by_name(canvas, QT_TO_UTF8(name)) : obs_get_source_by_name(QT_TO_UTF8(name));
}

QStringList DownstreamKeyer::CheckedScenes()
{
	QStringList checked;
	for (int i = 0; i < scenesList->count(); i++) {
		auto item = scenesList->item(i);
		if (item && item->checkState() == Qt::Checked)
			checked.append(item->text());
	}
	return checked;
}

QStringList DownstreamKeyer::InListOrder(const QStringList &scenes)
{
	QStringList ordered;
	for (int i = 0; i < scenesList->count(); i++) {
		auto item = scenesList->item(i);
		if (item && scenes.contains(item->text()))
			ordered.append(item->text());
	}
	return ordered;
}

// ---- video only wrapper: shows its child, but has no audio, so the child's audio is not mixed ----

#define VIDEO_ONLY_ID "downstream_keyer_video_only"

struct video_only_data {
	obs_source_t *child;
};

static const char *video_only_get_name(void *)
{
	return "Downstream Keyer Video Only";
}

static void *video_only_create(obs_data_t *, obs_source_t *)
{
	return new video_only_data{nullptr};
}

static void video_only_destroy(void *data)
{
	auto d = static_cast<video_only_data *>(data);
	obs_source_release(d->child);
	delete d;
}

static void video_only_render(void *data, gs_effect_t *)
{
	auto d = static_cast<video_only_data *>(data);
	if (d->child)
		obs_source_video_render(d->child);
}

static uint32_t video_only_width(void *data)
{
	auto d = static_cast<video_only_data *>(data);
	return d->child ? obs_source_get_width(d->child) : 0;
}

static uint32_t video_only_height(void *data)
{
	auto d = static_cast<video_only_data *>(data);
	return d->child ? obs_source_get_height(d->child) : 0;
}

static void video_only_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	auto d = static_cast<video_only_data *>(data);
	if (d->child)
		enum_callback(static_cast<obs_source_t *>(nullptr), d->child, param);
}

void dsk_register_video_only_source()
{
	static obs_source_info info = {};
	info.id = VIDEO_ONLY_ID;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CAP_DISABLED;
	info.get_name = video_only_get_name;
	info.create = video_only_create;
	info.destroy = video_only_destroy;
	info.video_render = video_only_render;
	info.get_width = video_only_width;
	info.get_height = video_only_height;
	info.enum_active_sources = video_only_enum_sources;
	info.enum_all_sources = video_only_enum_sources;
	obs_register_source(&info);
}

// a private video only source showing source (takes the reference to source)
static obs_source_t *wrap_video_only(const QString &name, obs_source_t *source)
{
	obs_source_t *wrapper = obs_source_create_private(VIDEO_ONLY_ID, QT_TO_UTF8(name), nullptr);
	if (!wrapper)
		return source;
	static_cast<video_only_data *>(obs_obj_get_data(wrapper))->child = source;
	return wrapper;
}

// One scene: that scene. Several: a private scene with them layered, the top of the list on top.
// Without Include Audio it is wrapped in a video only source.
obs_source_t *DownstreamKeyer::BuildSetSource(const QStringList &scenes)
{
	if (scenes.isEmpty())
		return nullptr;
	QString name = QT_UTF8(obs_module_text("DownstreamKeyer"));
	name += " ";
	name += objectName();
	obs_source_t *source = nullptr;
	if (scenes.size() == 1) {
		source = GetSourceByName(scenes.first());
	} else {
		obs_scene_t *scene = obs_scene_create_private(QT_TO_UTF8(name));
		for (auto it = scenes.crbegin(); it != scenes.crend(); ++it) {
			obs_source_t *child = GetSourceByName(*it);
			if (child)
				obs_scene_add(scene, child);
			obs_source_release(child);
		}
		source = obs_scene_get_source(scene);
	}
	if (!source || includeAudio)
		return source;
	return wrap_video_only(name, source);
}

void DownstreamKeyer::SetIncludeAudio(bool include)
{
	if (includeAudio == include)
		return;
	includeAudio = include;
	// rebuild what is on the output and in preview; the picture stays the same, so no transition
	if (liveSource) {
		obs_source_t *newSource = BuildSetSource(liveScenes);
		obs_source_release(liveSource);
		liveSource = newSource;
		apply_source(liveSource, false);
	}
	if (IsPreviewing()) {
		obs_source_release(previewSource);
		previewSource = BuildSetSource(previewScenes);
	}
	UpdatePreviewOverlay(true);
}

// Make scenes the live scenes and put them on the output. source, when set, is the ready made source for them.
void DownstreamKeyer::SetLiveScenes(const QStringList &scenes, obs_source_t *source, bool useTransition)
{
	if (scenes.isEmpty()) {
		KeyerOff();
		return;
	}
	if (source || scenes != liveScenes || !liveSource) {
		obs_source_t *newSource = source ? obs_source_get_ref(source) : BuildSetSource(scenes);
		obs_source_release(liveSource);
		liveSource = newSource;
		liveScenes = scenes;
	}
	lastLiveScenes = liveScenes;
	apply_source(liveSource, useTransition);
	UpdateTally();
}

void DownstreamKeyer::ScenesChecked()
{
	const auto checked = CheckedScenes();
	if (checked == checkedCache)
		return;
	checkedCache = checked;
	// a running preview follows the checked scenes
	if (IsPreviewing()) {
		if (checked.isEmpty()) {
			ClearPreview();
		} else {
			previewScenes = checked;
			obs_source_release(previewSource);
			previewSource = BuildSetSource(checked);
		}
	}
	UpdateTally();
}

void DownstreamKeyer::ChangeSceneIndex(bool relative, int offset, int invalidIdx)
{
	int idx = scenesList->currentRow();
	if (idx == -1 || idx == invalidIdx)
		return;

	scenesList->blockSignals(true);
	QListWidgetItem *item = scenesList->takeItem(idx);

	if (!relative)
		idx = 0;

	scenesList->insertItem(idx + offset, item);
	scenesList->setCurrentRow(idx + offset);
	item->setSelected(true);
	scenesList->blockSignals(false);
	ScenesChecked();
}

void DownstreamKeyer::AddToolbarWidget(QWidget *widget)
{
	auto spacer = new QWidget(scenesToolbar);
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	scenesToolbar->addWidget(spacer);
	scenesToolbar->addWidget(widget);
}

void DownstreamKeyer::Save(obs_data_t *data)
{
	obs_data_set_string(data, "transition", transition ? obs_source_get_name(transition) : "");
	obs_data_set_int(data, "transition_duration", transitionDuration);
	obs_data_set_string(data, "show_transition", showTransition ? obs_source_get_name(showTransition) : "");
	obs_data_set_int(data, "show_transition_duration", showTransitionDuration);
	obs_data_set_string(data, "hide_transition", hideTransition ? obs_source_get_name(hideTransition) : "");
	obs_data_set_int(data, "hide_transition_duration", hideTransitionDuration);
	obs_data_set_int(data, "hide_after", hideAfter);
	obs_data_set_bool(data, "include_audio", includeAudio);
	obs_data_array_t *sceneArray = obs_data_array_create();
	for (int i = 0; i < scenesList->count(); i++) {
		auto item = scenesList->item(i);
		if (!item)
			continue;
		auto sceneData = obs_data_create();
		obs_data_set_string(sceneData, "name", QT_TO_UTF8(item->text()));
		obs_data_set_bool(sceneData, "checked", item->checkState() == Qt::Checked);
		obs_data_array_push_back(sceneArray, sceneData);
		obs_data_release(sceneData);
	}
	obs_data_set_array(data, "scenes", sceneArray);
	obs_data_array_release(sceneArray);
	// "scene" is kept for older versions and websocket clients: the first live scene
	obs_data_set_string(data, "scene", liveScenes.isEmpty() ? "" : QT_TO_UTF8(liveScenes.first()));
	auto live = obs_data_array_create();
	for (const auto &s : liveScenes) {
		auto obj = obs_data_create();
		obs_data_set_string(obj, "name", QT_TO_UTF8(s));
		obs_data_array_push_back(live, obj);
		obs_data_release(obj);
	}
	obs_data_set_array(data, "live_scenes", live);
	obs_data_array_release(live);

	obs_data_array_t *nh = obs_hotkey_save(null_hotkey_id);
	obs_data_set_array(data, "null_hotkey", nh);
	obs_data_array_release(nh);
	obs_data_array_t *glh = obs_hotkey_save(go_live_hotkey_id);
	obs_data_set_array(data, "go_live_hotkey", glh);
	obs_data_array_release(glh);
	obs_data_array_t *ph = obs_hotkey_save(preview_hotkey_id);
	obs_data_set_array(data, "preview_hotkey", ph);
	obs_data_array_release(ph);
	auto excludes = obs_data_array_create();
	for (auto t : exclude_scenes) {
		const auto obj = obs_data_create();
		obs_data_set_string(obj, "name", t.c_str());
		obs_data_array_push_back(excludes, obj);
		obs_data_release(obj);
	}
	obs_data_set_array(data, "exclude_scenes", excludes);
	obs_data_array_release(excludes);
	auto loads = obs_data_array_create();
	for (const auto &l : load_scenes) {
		const auto obj = obs_data_create();
		obs_data_set_string(obj, "name", l.c_str());
		obs_data_array_push_back(loads, obj);
		obs_data_release(obj);
	}
	obs_data_set_array(data, "load_scenes", loads);
	obs_data_array_release(loads);
}

std::string DownstreamKeyer::GetTransition(enum transitionType transition_type)
{
	if (transition_type == transitionType::match && transition)
		return obs_source_get_name(transition);
	if (transition_type == transitionType::show && showTransition)
		return obs_source_get_name(showTransition);
	if (transition_type == transitionType::hide && hideTransition)
		return obs_source_get_name(hideTransition);
	if (transition_type == transitionType::override && overrideTransition)
		return obs_source_get_name(overrideTransition);
	return "";
}

void DownstreamKeyer::SetTransition(const char *transition_name, enum transitionType transition_type)
{
	obs_source_t *oldTransition = transition;
	if (transition_type == transitionType::show)
		oldTransition = showTransition;
	else if (transition_type == transitionType::hide)
		oldTransition = hideTransition;
	else if (transition_type == transitionType::override)
		oldTransition = overrideTransition;

	if (!oldTransition && (!transition_name || !strlen(transition_name)))
		return;

	obs_source_t *newTransition = nullptr;
	obs_frontend_source_list transitions = {};
	get_transitions(get_transitions_data, &transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		const char *n = obs_source_get_name(transitions.sources.array[i]);
		if (!n)
			continue;
		if (strcmp(transition_name, n) == 0) {
			newTransition = obs_source_duplicate(transitions.sources.array[i],
							     obs_source_get_name(transitions.sources.array[i]), true);
			break;
		}
	}
	obs_frontend_source_list_free(&transitions);

	if (transition_type == transitionType::show)
		showTransition = newTransition;
	else if (transition_type == transitionType::hide)
		hideTransition = newTransition;
	else if (transition_type == transitionType::override)
		overrideTransition = newTransition;
	else
		transition = newTransition;
	obs_source_t *prevSource = view     ? obs_view_get_source(view, outputChannel)
				   : canvas ? obs_canvas_get_channel(canvas, outputChannel)
					    : obs_get_output_source(outputChannel);
	if (oldTransition && prevSource == oldTransition) {
		if (newTransition) {
			//swap transition
			obs_transition_swap_begin(newTransition, oldTransition);
			set_output(newTransition);
			obs_transition_swap_end(newTransition, oldTransition);
		} else {
			set_output(liveSource);
		}
	}
	obs_source_release(prevSource);
	if (oldTransition) {
		obs_transition_clear(oldTransition);
		obs_source_release(oldTransition);
	}
}

void DownstreamKeyer::SetTransitionDuration(int duration, enum transitionType transition_type)
{
	if (transition_type == match)
		transitionDuration = duration;
	else if (transition_type == transitionType::show)
		showTransitionDuration = duration;
	else if (transition_type == transitionType::hide)
		hideTransitionDuration = duration;
	else if (transition_type == transitionType::override)
		overrideTransitionDuration = duration;
}

int DownstreamKeyer::GetTransitionDuration(enum transitionType transition_type)
{
	if (transition_type == transitionType::show)
		return showTransitionDuration;
	if (transition_type == transitionType::hide)
		return hideTransitionDuration;
	if (transition_type == transitionType::override)
		return overrideTransitionDuration;
	return transitionDuration;
}

void DownstreamKeyer::SetHideAfter(int duration)
{
	hideAfter = duration;
	if (duration == 0)
		hideTimer.stop();
}

int DownstreamKeyer::GetHideAfter()
{
	return hideAfter;
}

SceneChangeContext DownstreamKeyer::GetContext(const std::string &scene, bool forPreview)
{
	return contextProvider ? contextProvider(scene, forPreview) : SceneChangeContext();
}

// Load On Scene: the last live scenes, or the first scene in the list
QStringList DownstreamKeyer::AutoLoadScenes()
{
	auto scenes = InListOrder(lastLiveScenes);
	if (scenes.isEmpty() && scenesList->count() > 0)
		scenes.append(scenesList->item(0)->text());
	return scenes;
}

// are the live scenes on the output right now (not hidden by an excluded or unlisted program scene)
bool DownstreamKeyer::IsLiveOnOutput()
{
	if (!liveSource)
		return false;
	obs_source_t *current = view     ? obs_view_get_source(view, outputChannel)
				: canvas ? obs_canvas_get_channel(canvas, outputChannel)
					 : obs_get_output_source(outputChannel);
	if (current && obs_source_get_type(current) == OBS_SOURCE_TYPE_TRANSITION) {
		obs_source_t *active = obs_transition_get_active_source(current);
		obs_source_release(current);
		current = active;
	}
	const bool on = current == liveSource;
	obs_source_release(current);
	return on;
}

void DownstreamKeyer::SceneChanged(std::string scene)
{
	SceneChanged(scene, GetContext(scene, false));
}

void DownstreamKeyer::SceneChanged(const std::string &scene, const SceneChangeContext &ctx)
{
	const bool previewTake = IsPreviewTakeTo(scene);

	// Hold Keyers: nothing changes. A cue still rides in with the scene and goes live when the transition ends.
	if (ctx.hold)
		return;

	if (IsSceneExcluded(scene.c_str())) {
		// the live scenes are kept, they come back on the next scene the keyer is shown on
		if (previewTake) {
			RemoveOverlay(true);
			ClearPreview();
		}
		apply_source(nullptr);
		return;
	}

	// the cued scenes go on program inside the scene, and on the keyer output when the transition ends
	if (previewTake)
		return;

	const bool listed = IsLoadedOn(scene);

	// Keyer Off at Scene Change, or another keyer's cue is taken: only Load On Scene keyers of this scene stay
	if (ctx.clearOthers && !listed) {
		KeyerOff();
		return;
	}

	// Load On Scene, not a listed scene: off for now, the live scenes come back on a listed scene
	if (!load_scenes.empty() && !listed) {
		apply_source(nullptr);
		return;
	}

	// a listed scene and nothing live (turned off, or Hide After ran): load the last live scenes again
	if (listed && liveScenes.isEmpty()) {
		SetLiveScenes(AutoLoadScenes());
		return;
	}

	// puts the live scenes back after an excluded scene, nothing changes if they are already on
	apply_live_source();
}

void DownstreamKeyer::SyncToProgramScene(const std::string &scene)
{
	if (IsLoadedOn(scene) && !IsSceneExcluded(scene.c_str())) {
		if (liveScenes.isEmpty())
			SetLiveScenes(AutoLoadScenes());
		else
			apply_live_source();
		return;
	}
	KeyerOff();
}

void DownstreamKeyer::Load(obs_data_t *data)
{
	SetTransition(obs_data_get_string(data, "transition"));
	transitionDuration = obs_data_get_int(data, "transition_duration");
	SetTransition(obs_data_get_string(data, "show_transition"), transitionType::show);
	showTransitionDuration = obs_data_get_int(data, "show_transition_duration");
	SetTransition(obs_data_get_string(data, "hide_transition"), transitionType::hide);
	hideTransitionDuration = obs_data_get_int(data, "hide_transition_duration");
	hideAfter = obs_data_get_int(data, "hide_after");
	includeAudio = obs_data_get_bool(data, "include_audio");

	// live scenes: this version saves a list, earlier versions the one scene that was on the output
	QStringList live;
	obs_data_array_t *liveArray = obs_data_get_array(data, "live_scenes");
	if (liveArray) {
		const auto count = obs_data_array_count(liveArray);
		for (size_t i = 0; i < count; i++) {
			auto obj = obs_data_array_item(liveArray, i);
			live.append(QT_UTF8(obs_data_get_string(obj, "name")));
			obs_data_release(obj);
		}
		obs_data_array_release(liveArray);
	} else if (obs_data_has_user_value(data, "live_scene")) {
		const auto s = QT_UTF8(obs_data_get_string(data, "live_scene"));
		if (!s.isEmpty())
			live.append(s);
	} else {
		const auto s = QT_UTF8(obs_data_get_string(data, "scene"));
		if (!s.isEmpty())
			live.append(s);
	}

	scenesList->blockSignals(true);
	scenesList->clear();
	obs_data_array_t *sceneArray = obs_data_get_array(data, "scenes");
	if (sceneArray) {
		auto count = obs_data_array_count(sceneArray);
		for (size_t i = 0; i < count; i++) {
			const auto sceneData = obs_data_array_item(sceneArray, i);
			const auto source_name = obs_data_get_string(sceneData, "name");
			const auto item = new QListWidgetItem(QT_UTF8(source_name));
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
			item->setCheckState(obs_data_get_bool(sceneData, "checked") ? Qt::Checked : Qt::Unchecked);
			scenesList->addItem(item);
			obs_source_t *source = canvas ? obs_canvas_get_source_by_name(canvas, source_name)
						      : obs_get_source_by_name(source_name);
			obs_data_release(sceneData);

			if (source) {
				std::string enable_hotkey = obs_module_text("EnableDSK");
				enable_hotkey += " ";
				enable_hotkey += QT_TO_UTF8(objectName());
				std::string disable_hotkey = obs_module_text("DisableDSK");
				disable_hotkey += " ";
				disable_hotkey += QT_TO_UTF8(objectName());
				uint64_t h = obs_hotkey_pair_register_source(source, enable_hotkey.c_str(), enable_hotkey.c_str(),
									     disable_hotkey.c_str(), disable_hotkey.c_str(),
									     enable_DSK_hotkey, disable_DSK_hotkey, this, this);

				if (h != OBS_INVALID_HOTKEY_PAIR_ID) {
					item->setData(Qt::UserRole, static_cast<uint>(h));
				}
				obs_source_release(source);
			}
		}
		obs_data_array_release(sceneArray);
	}
	scenesList->setCurrentRow(-1);
	scenesList->blockSignals(false);
	checkedCache = CheckedScenes();

	liveScenes = InListOrder(live);
	lastLiveScenes = liveScenes;
	obs_source_release(liveSource);
	liveSource = BuildSetSource(liveScenes);
	if (liveSource)
		set_output(liveSource);

	obs_data_array_t *nh = obs_data_get_array(data, "null_hotkey");
	obs_hotkey_load(null_hotkey_id, nh);
	obs_data_array_release(nh);
	obs_data_array_t *glh = obs_data_get_array(data, "go_live_hotkey");
	obs_hotkey_load(go_live_hotkey_id, glh);
	obs_data_array_release(glh);
	obs_data_array_t *ph = obs_data_get_array(data, "preview_hotkey");
	obs_hotkey_load(preview_hotkey_id, ph);
	obs_data_array_release(ph);

	auto excludes = obs_data_get_array(data, "exclude_scenes");
	exclude_scenes.clear();
	if (excludes) {
		auto count = obs_data_array_count(excludes);
		for (size_t i = 0; i < count; i++) {
			const auto sceneData = obs_data_array_item(excludes, i);
			exclude_scenes.emplace(obs_data_get_string(sceneData, "name"));
			obs_data_release(sceneData);
		}
		obs_data_array_release(excludes);
	}

	auto loads = obs_data_get_array(data, "load_scenes");
	load_scenes.clear();
	if (loads) {
		auto count = obs_data_array_count(loads);
		for (size_t i = 0; i < count; i++) {
			const auto sceneData = obs_data_array_item(loads, i);
			const char *name = obs_data_get_string(sceneData, "name");
			if (!name || !*name) // settings saved by the earlier per-scene mapping version
				name = obs_data_get_string(sceneData, "program_scene");
			if (name && *name)
				load_scenes.emplace(name);
			obs_data_release(sceneData);
		}
		obs_data_array_release(loads);
	}
	UpdateTally();
}

void DownstreamKeyer::source_rename(void *data, calldata_t *calldata)
{
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	const auto newName = QT_UTF8(calldata_string(calldata, "new_name"));
	const auto prevName = QT_UTF8(calldata_string(calldata, "prev_name"));
	const auto count = downstreamKeyer->scenesList->count();
	for (int i = 0; i < count; i++) {
		const auto item = downstreamKeyer->scenesList->item(i);
		if (item->text() == prevName)
			item->setText(newName);
	}
	downstreamKeyer->liveScenes.replaceInStrings(QRegularExpression("^" + QRegularExpression::escape(prevName) + "$"), newName);
	downstreamKeyer->lastLiveScenes.replaceInStrings(QRegularExpression("^" + QRegularExpression::escape(prevName) + "$"),
							 newName);
	downstreamKeyer->previewScenes.replaceInStrings(QRegularExpression("^" + QRegularExpression::escape(prevName) + "$"),
							newName);
	downstreamKeyer->checkedCache = downstreamKeyer->CheckedScenes();
}

void DownstreamKeyer::source_remove(void *data, calldata_t *calldata)
{
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	const auto name = QT_UTF8(obs_source_get_name(static_cast<obs_source_t *>(calldata_ptr(calldata, "source"))));

	const auto count = downstreamKeyer->scenesList->count();
	bool found = false;
	for (int i = count - 1; i >= 0; i--) {
		const auto item = downstreamKeyer->scenesList->item(i);
		if (item->text() == name) {
			downstreamKeyer->scenesList->removeItemWidget(item);
			obs_hotkey_pair_unregister(item->data(Qt::UserRole).toUInt());
			delete item;
			found = true;
		}
	}
	if (!found)
		return;
	// layered live scenes drop the removed scene by themselves, a single live scene goes off
	downstreamKeyer->lastLiveScenes.removeAll(name);
	if (downstreamKeyer->liveScenes.removeAll(name) && downstreamKeyer->liveScenes.isEmpty())
		QMetaObject::invokeMethod(downstreamKeyer, "KeyerOff", Qt::QueuedConnection);
	QMetaObject::invokeMethod(downstreamKeyer, [downstreamKeyer]() { downstreamKeyer->ScenesChecked(); }, Qt::QueuedConnection);
}

// scene hotkeys put a scene on or off the output right away, next to the other live scenes
void DownstreamKeyer::SetSceneHotkey(obs_hotkey_pair_id id, bool show)
{
	for (int i = 0; i < scenesList->count(); i++) {
		auto item = scenesList->item(i);
		if (!item || item->data(Qt::UserRole).toUInt() != id)
			continue;
		const auto name = item->text();
		item->setCheckState(show ? Qt::Checked : Qt::Unchecked);
		QStringList scenes = liveScenes;
		if (show && !scenes.contains(name))
			scenes.append(name);
		else if (!show)
			scenes.removeAll(name);
		SetLiveScenes(InListOrder(scenes));
		return;
	}
}

bool DownstreamKeyer::enable_DSK_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	QMetaObject::invokeMethod(
		downstreamKeyer, [downstreamKeyer, id]() { downstreamKeyer->SetSceneHotkey(id, true); }, Qt::QueuedConnection);
	return true;
}

bool DownstreamKeyer::disable_DSK_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	QMetaObject::invokeMethod(
		downstreamKeyer, [downstreamKeyer, id]() { downstreamKeyer->SetSceneHotkey(id, false); }, Qt::QueuedConnection);
	return true;
}

void DownstreamKeyer::null_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	QMetaObject::invokeMethod(downstreamKeyer, "KeyerOff", Qt::QueuedConnection);
}

void DownstreamKeyer::go_live_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	QMetaObject::invokeMethod(static_cast<DownstreamKeyer *>(data), "GoLive", Qt::QueuedConnection);
}

void DownstreamKeyer::preview_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	QMetaObject::invokeMethod(static_cast<DownstreamKeyer *>(data), "TogglePreview", Qt::QueuedConnection);
}

std::string DownstreamKeyer::GetProgramSceneName()
{
	obs_source_t *scene = nullptr;
	if (view || canvas) {
		obs_source_t *source = view ? obs_view_get_source(view, 0) : obs_canvas_get_channel(canvas, 0);
		while (source && obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts = obs_transition_get_active_source(source);
			if (!ts)
				break;
			obs_source_release(source);
			source = ts;
		}
		if (source && obs_source_is_scene(source))
			scene = source;
		else
			obs_source_release(source);
	} else {
		scene = obs_frontend_get_current_scene();
	}
	std::string name = scene ? obs_source_get_name(scene) : "";
	obs_source_release(scene);
	return name;
}

void DownstreamKeyer::AddExcludeScene(const char *scene_name)
{
	if (exclude_scenes.count(scene_name) > 0)
		return;
	exclude_scenes.emplace(scene_name);
	if (GetProgramSceneName() == scene_name)
		SceneChanged(scene_name);
	UpdatePreviewOverlay();
}

void DownstreamKeyer::RemoveExcludeScene(const char *scene_name)
{
	exclude_scenes.erase(scene_name);
	if (GetProgramSceneName() == scene_name)
		SceneChanged(scene_name);
	UpdatePreviewOverlay();
}

bool DownstreamKeyer::IsSceneExcluded(const char *scene_name)
{
	return exclude_scenes.find(scene_name) != exclude_scenes.end();
}

void DownstreamKeyer::AddLoadScene(const char *scene_name)
{
	if (!scene_name || !*scene_name || !load_scenes.emplace(scene_name).second)
		return;
	// the list affects every scene (not just this one), so always re-evaluate
	SceneChanged(GetProgramSceneName());
	UpdatePreviewOverlay();
}

void DownstreamKeyer::RemoveLoadScene(const char *scene_name)
{
	if (!scene_name || !load_scenes.erase(scene_name))
		return;
	SceneChanged(GetProgramSceneName());
	UpdatePreviewOverlay();
}

bool DownstreamKeyer::IsLoadScene(const char *scene_name)
{
	return load_scenes.find(scene_name) != load_scenes.end();
}

QString DownstreamKeyer::GetScene()
{
	return liveScenes.isEmpty() ? QString() : liveScenes.first();
}

// check only this scene
bool DownstreamKeyer::ArmScene(const QString &scene_name)
{
	if (scene_name.isEmpty())
		return true;
	return ArmScenes({scene_name});
}

// are all these scenes in the keyer's list
bool DownstreamKeyer::HasScenes(const QStringList &scenes)
{
	for (const auto &s : scenes) {
		if (scenesList->findItems(s, Qt::MatchFixedString).isEmpty())
			return false;
	}
	return true;
}

// check exactly these scenes (websocket dsk_go_live / dsk_go_to_preview)
bool DownstreamKeyer::ArmScenes(const QStringList &scenes)
{
	if (!HasScenes(scenes))
		return false;
	{
		const QSignalBlocker blocker(scenesList);
		for (int i = 0; i < scenesList->count(); i++) {
			auto item = scenesList->item(i);
			item->setCheckState(scenes.contains(item->text()) ? Qt::Checked : Qt::Unchecked);
		}
	}
	ScenesChecked();
	return true;
}

// websocket dsk_select_scene: this scene only goes live right away
bool DownstreamKeyer::SwitchToScene(QString scene_name)
{
	if (scene_name.isEmpty()) {
		KeyerOff();
		return true;
	}
	if (!ArmScene(scene_name))
		return false;
	SetLiveScenes({scene_name});
	return true;
}

void DownstreamKeyer::add_scene(QString scene_name, obs_source_t *s, int insertBeforeRow)
{
	const auto item = new QListWidgetItem(scene_name);
	item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
	item->setCheckState(Qt::Unchecked);
	int scenesListCount = scenesList->count();
	if ((insertBeforeRow > scenesListCount) || (insertBeforeRow < 0)) {
		insertBeforeRow = scenesListCount;
	}
	scenesList->insertItem(insertBeforeRow, item);

	std::string enable_hotkey = obs_module_text("EnableDSK");
	enable_hotkey += " ";
	enable_hotkey += QT_TO_UTF8(objectName());
	std::string disable_hotkey = obs_module_text("DisableDSK");
	disable_hotkey += " ";
	disable_hotkey += QT_TO_UTF8(objectName());
	uint64_t h = obs_hotkey_pair_register_source(s, enable_hotkey.c_str(), enable_hotkey.c_str(), disable_hotkey.c_str(),
						     disable_hotkey.c_str(), enable_DSK_hotkey, disable_DSK_hotkey, this, this);

	if (h != OBS_INVALID_HOTKEY_PAIR_ID) {
		item->setData(Qt::UserRole, static_cast<uint>(h));
	}
}

bool DownstreamKeyer::AddScene(QString scene_name, int insertBeforeRow)
{
	if (scene_name.isEmpty()) {
		return false;
	}
	if (scenesList->findItems(scene_name, Qt::MatchFixedString).count() != 0) {
		return true;
	}
	auto nameUtf8 = scene_name.toUtf8();
	auto name = nameUtf8.constData();
	auto s = canvas ? obs_canvas_get_source_by_name(canvas, name) : obs_get_source_by_name(name);
	if (obs_source_is_scene(s)) {
		add_scene(scene_name, s, insertBeforeRow);
		obs_source_release(s);
		return true;
	}
	obs_source_release(s);
	return false;
}

bool DownstreamKeyer::RemoveScene(QString scene_name)
{
	if (scene_name.isEmpty()) {
		return false;
	}
	for (int i = 0; i < scenesList->count(); i++) {
		const auto item = scenesList->item(i);
		if (!item)
			continue;

		if (item->text() == scene_name) {
			scenesList->removeItemWidget(item);
			obs_hotkey_pair_unregister(item->data(Qt::UserRole).toUInt());
			delete item;
			ScenesChecked();
			return true;
		}
	}
	return false;
}

void DownstreamKeyer::SetOutputChannel(int oc)
{
	if (oc == outputChannel)
		return;
	obs_source_t *prevSource = view     ? obs_view_get_source(view, outputChannel)
				   : canvas ? obs_canvas_get_channel(canvas, outputChannel)
					    : obs_get_output_source(outputChannel);
	obs_source_t *prevTransition = nullptr;
	if (prevSource && obs_source_get_type(prevSource) == OBS_SOURCE_TYPE_TRANSITION) {
		prevTransition = prevSource;
		prevSource = obs_transition_get_active_source(prevSource);
	}
	if (prevTransition) {
		if (prevTransition == transition || prevTransition == showTransition || prevTransition == hideTransition ||
		    prevTransition == overrideTransition || prevTransition == defaultTransition) {
			set_output(nullptr);
		} else {
			obs_source_release(prevTransition);
			prevTransition = nullptr;
		}
	} else if (prevSource) {
		const auto newSource = liveSource ? obs_source_get_ref(liveSource) : nullptr;
		if (prevSource == newSource) {
			set_output(nullptr);
			obs_source_release(newSource);
		} else {
			obs_source_release(prevSource);
			prevSource = newSource;
		}
	}
	outputChannel = oc;
	if (prevTransition) {
		set_output(prevTransition);
	} else {
		apply_live_source();
	}
	obs_source_release(prevSource);
	obs_source_release(prevTransition);
}

// ---- Go Live and Go to Preview ----

obs_source_t *dsk_get_main_program_scene()
{
	obs_source_t *source = obs_get_output_source(0);
	while (source && obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
		obs_source_t *active = obs_transition_get_active_source(source);
		obs_source_release(source);
		source = active;
	}
	return source;
}

static bool main_transition_running()
{
	obs_source_t *transition = obs_get_output_source(0);
	bool running = false;
	if (transition && obs_source_get_type(transition) == OBS_SOURCE_TYPE_TRANSITION) {
		const float t = obs_transition_get_time(transition);
		running = t > 0.0f && t < 1.0f;
	}
	obs_source_release(transition);
	return running;
}

struct preview_items_ctx {
	const char *owner;
	std::vector<obs_sceneitem_t *> items;
};

void dsk_remove_preview_items(obs_source_t *scene_source, const char *owner)
{
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		return;
	preview_items_ctx ctx{owner, {}};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto c = static_cast<preview_items_ctx *>(param);
			obs_data_t *priv = obs_sceneitem_get_private_settings(item);
			if (obs_data_get_bool(priv, "dsk_preview") &&
			    (!c->owner || strcmp(obs_data_get_string(priv, "dsk_preview_owner"), c->owner) == 0)) {
				obs_sceneitem_addref(item);
				c->items.push_back(item);
			}
			obs_data_release(priv);
			return true;
		},
		&ctx);
	for (auto item : ctx.items) {
		obs_sceneitem_remove(item);
		obs_sceneitem_release(item);
	}
}

void dsk_purge_preview_items()
{
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++)
		dsk_remove_preview_items(scenes.sources.array[i], nullptr);
	obs_frontend_source_list_free(&scenes);
}

static std::atomic<bool> program_transition_active{false};

void dsk_set_program_transition_active(bool active)
{
	program_transition_active = active;
}

// stack the preview items of all keyers by output channel, like the keyer outputs are stacked
static void sort_preview_items(obs_source_t *scene_source)
{
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		return;
	std::vector<std::pair<long long, obs_sceneitem_t *>> items;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto list = static_cast<std::vector<std::pair<long long, obs_sceneitem_t *>> *>(param);
			obs_data_t *priv = obs_sceneitem_get_private_settings(item);
			if (obs_data_get_bool(priv, "dsk_preview")) {
				obs_sceneitem_addref(item);
				list->emplace_back(obs_data_get_int(priv, "dsk_preview_channel"), item);
			}
			obs_data_release(priv);
			return true;
		},
		&items);
	std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	for (auto &i : items) {
		obs_sceneitem_set_order(i.second, OBS_ORDER_MOVE_TOP);
		obs_sceneitem_release(i.second);
	}
}

bool DownstreamKeyer::PreviewSupported()
{
	// studio mode only exists for the main OBS output
	return !view && !canvas;
}

void DownstreamKeyer::GoLive()
{
	// a cued preview is what goes live, otherwise the checked scenes
	if (IsPreviewing()) {
		const auto scenes = previewScenes;
		obs_source_t *source = obs_source_get_ref(previewSource);
		ClearPreview();
		SetLiveScenes(scenes, source);
		obs_source_release(source);
		return;
	}
	SetLiveScenes(CheckedScenes());
}

void DownstreamKeyer::TogglePreview()
{
	if (IsPreviewing()) {
		ClearPreview();
		return;
	}
	const auto checked = CheckedScenes();
	if (!PreviewSupported() || !obs_frontend_preview_program_mode_active() || checked.isEmpty()) {
		UpdateButtons();
		return;
	}
	previewScenes = checked;
	obs_source_release(previewSource);
	previewSource = BuildSetSource(checked);
	UpdateTally();
}

// What this keyer shows after a program transition to scene, the same way SceneChanged works it out.
QStringList DownstreamKeyer::PredictedScenes(const std::string &scene, const SceneChangeContext &ctx)
{
	const bool excluded = IsSceneExcluded(scene.c_str());
	if (IsPreviewing())
		return excluded ? QStringList() : previewScenes;
	if (ctx.hold)
		return IsLiveOnOutput() ? liveScenes : QStringList();
	if (excluded)
		return {};
	const bool listed = IsLoadedOn(scene);
	if (ctx.clearOthers && !listed)
		return {};
	if (!load_scenes.empty() && !listed)
		return {};
	if (listed && liveScenes.isEmpty())
		return AutoLoadScenes();
	return liveScenes;
}

void DownstreamKeyer::UpdatePreviewOverlay(bool force)
{
	if (!PreviewSupported() || program_transition_active)
		return;
	obs_source_t *previewScene = obs_frontend_preview_program_mode_active() ? obs_frontend_get_current_preview_scene()
										: nullptr;
	const QStringList next = previewScene ? PredictedScenes(obs_source_get_name(previewScene),
								GetContext(obs_source_get_name(previewScene), true))
					      : QStringList();
	// reuse the cued or live source when the overlay shows exactly that
	obs_source_t *wanted = IsPreviewing() ? previewSource : (next == liveScenes ? liveSource : nullptr);

	obs_source_t *considered = obs_weak_source_get_source(overlayPreviewScene);
	const bool unchanged = !force && considered == previewScene && next == overlayScenes && (!wanted || wanted == overlayChild);
	obs_source_release(considered);
	if (unchanged) {
		obs_source_release(previewScene);
		return;
	}

	RemoveOverlay(false);
	if (previewScene && !next.isEmpty()) {
		overlayScenes = next;
		overlayPreviewScene = obs_source_get_weak_source(previewScene);
		overlayChild = wanted ? obs_source_get_ref(wanted) : BuildSetSource(next);

		obs_source_t *program = dsk_get_main_program_scene();
		obs_scene_t *scene = obs_scene_from_source(previewScene);
		// never add to the scene that is on program right now, that would put the keyer scenes on air
		if (scene && overlayChild && previewScene != program && overlayChild != previewScene) {
			QString name = QT_UTF8(obs_module_text("DownstreamKeyer"));
			name += " ";
			name += objectName();
			// always video only and private, so a saved scene collection never keeps a real scene item
			obs_source_t *itemSource = wrap_video_only(name, obs_source_get_ref(overlayChild));
			obs_sceneitem_t *item = obs_scene_add(scene, itemSource);
			obs_source_release(itemSource);
			if (item) {
				obs_data_t *priv = obs_sceneitem_get_private_settings(item);
				obs_data_set_bool(priv, "dsk_preview", true);
				obs_data_set_string(priv, "dsk_preview_owner", previewToken.c_str());
				obs_data_set_int(priv, "dsk_preview_channel", outputChannel);
				obs_data_release(priv);
				obs_sceneitem_set_locked(item, true);
				overlayTarget = obs_source_get_weak_source(previewScene);
				sort_preview_items(previewScene);
			}
		}
		if (program)
			overlayProgramAtAttach = obs_source_get_weak_source(program);
		obs_source_release(program);
	}
	obs_source_release(previewScene);
}

void DownstreamKeyer::RemoveOverlay(bool everywhere)
{
	obs_source_t *target = obs_weak_source_get_source(overlayTarget);
	if (target)
		dsk_remove_preview_items(target, previewToken.c_str());
	obs_source_release(target);
	if (everywhere) {
		// with scene duplication the program scene is a copy that carries the item as well
		obs_source_t *program = dsk_get_main_program_scene();
		if (program)
			dsk_remove_preview_items(program, previewToken.c_str());
		obs_source_release(program);
		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++)
			dsk_remove_preview_items(scenes.sources.array[i], previewToken.c_str());
		obs_frontend_source_list_free(&scenes);
	}
	obs_weak_source_release(overlayTarget);
	overlayTarget = nullptr;
	obs_weak_source_release(overlayPreviewScene);
	overlayPreviewScene = nullptr;
	obs_weak_source_release(overlayProgramAtAttach);
	overlayProgramAtAttach = nullptr;
	obs_source_release(overlayChild);
	overlayChild = nullptr;
	overlayScenes.clear();
}

void DownstreamKeyer::ClearPreview()
{
	if (!IsPreviewing() && !previewSource) {
		UpdateButtons();
		return;
	}
	obs_source_release(previewSource);
	previewSource = nullptr;
	previewScenes.clear();
	UpdateTally(); // the overlay goes back to what the keyer shows without a cue
}

void DownstreamKeyer::StudioModeChanged()
{
	if (!obs_frontend_preview_program_mode_active())
		ClearPreview();
	UpdatePreviewOverlay(true);
	UpdateButtons();
}

void DownstreamKeyer::PreviewSceneChanged()
{
	// with "swap scenes" the preview changes at the end of the transition that took the cue live
	CheckPreviewTake();
	UpdatePreviewOverlay();
}

// is the program transition to scene taking the cued keyer scenes live
bool DownstreamKeyer::IsPreviewTakeTo(const std::string &scene)
{
	if (!IsPreviewing() || !overlayTarget)
		return false;
	obs_source_t *target = obs_weak_source_get_source(overlayTarget);
	const bool take = target && scene == obs_source_get_name(target);
	obs_source_release(target);
	return take;
}

void DownstreamKeyer::ProgramTransitionStarting(obs_source_t *dest)
{
	if (!PreviewSupported() || !dest)
		return;
	// a cue rides in with the scene and moves to the keyer output when the transition ends
	if (IsPreviewTakeTo(obs_source_get_name(dest)))
		return;
	// anything else is shown by the keyer output itself, so take the preview copy out before it is drawn twice
	dsk_remove_preview_items(dest, previewToken.c_str());
	RemoveOverlay(false);
}

void DownstreamKeyer::CheckPreviewTake(bool transitionEnded)
{
	if (!IsPreviewing() || !overlayTarget || (!transitionEnded && main_transition_running()))
		return;
	obs_source_t *program = dsk_get_main_program_scene();
	obs_source_t *target = obs_weak_source_get_source(overlayTarget);
	obs_source_t *programAtAttach = obs_weak_source_get_source(overlayProgramAtAttach);
	// a new program scene (a copy of it with scene duplication) with the name of the previewed scene
	const bool taken = program && target && program != programAtAttach &&
			   strcmp(obs_source_get_name(program), obs_source_get_name(target)) == 0;
	const std::string programName = program ? obs_source_get_name(program) : "";
	obs_source_release(programAtAttach);
	obs_source_release(target);
	obs_source_release(program);
	if (!taken)
		return;

	if (IsSceneExcluded(programName.c_str())) {
		RemoveOverlay(true);
		ClearPreview();
		return;
	}

	// The cued scenes went on program inside the scene. Put the same picture on the keyer output without a
	// transition, so it looks the same, then take the scene item out. Hide After starts now.
	const auto scenes = previewScenes;
	obs_source_t *source = obs_source_get_ref(previewSource);
	SetLiveScenes(scenes, source, false);
	obs_source_release(source);
	RemoveOverlay(true);
	ClearPreview();
}

void DownstreamKeyer::UpdateTally()
{
	const QBrush liveBrush(QColor(230, 60, 60));
	const QBrush previewBrush(QColor(70, 190, 100));
	const QSignalBlocker blocker(scenesList);
	for (int i = 0; i < scenesList->count(); i++) {
		auto item = scenesList->item(i);
		const bool live = liveScenes.contains(item->text());
		const bool preview = previewScenes.contains(item->text());
		QFont font = item->font();
		font.setBold(live || preview);
		item->setFont(font);
		if (live)
			item->setForeground(liveBrush);
		else if (preview)
			item->setForeground(previewBrush);
		else
			item->setData(Qt::ForegroundRole, QVariant());
	}
	scenesList->viewport()->update();
	UpdateButtons();
	// what this keyer shows can change what the other keyers show on the preview (a cue clears them), so the
	// dock refreshes every keyer's preview
	if (stateChanged)
		stateChanged();
	else
		UpdatePreviewOverlay();
}

void DownstreamKeyer::UpdateButtons()
{
	const bool studio = PreviewSupported() && obs_frontend_preview_program_mode_active();
	// Studio Mode: Go to Preview then Go to Program. Otherwise Go Live.
	goLiveButton->setVisible(!studio);
	previewButton->setVisible(studio);
	transitionKeyersButton->setVisible(studio);
	// Go to Preview and Go to Program light up while this keyer has scenes checked (or a cue to take)
	const bool ready = IsPreviewing() || !CheckedScenes().isEmpty();
	transitionKeyersButton->setEnabled(ready);
	previewButton->setEnabled(ready);
	transitionKeyersButton->setStyleSheet(ready ? "background-color: #b71c1c; color: white; font-weight: bold;" : "");

	const bool previewing = IsPreviewing();
	const auto armed = previewing ? previewScenes : CheckedScenes();
	// Go Live lights up when it has something new to take live
	const bool goLiveArmed = !armed.isEmpty() && armed != liveScenes;
	goLiveButton->setStyleSheet(goLiveArmed ? "background-color: #b71c1c; color: white; font-weight: bold;" : "");
	previewButton->setChecked(previewing);
	previewButton->setStyleSheet(ready ? "background-color: #1b5e20; color: white; font-weight: bold;" : "");
}
