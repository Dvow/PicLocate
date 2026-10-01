#include "ui/window.h"
namespace piclocate {
QString styleSheet() {
    return QStringLiteral(R"(
QWidget { color:#dce5f1; font-family:'Segoe UI'; font-size:13px; }
QMainWindow, QDialog { background:#0c131e; }
QWidget#Sidebar { background:#101925; border-right:1px solid #243043; }
QLabel { background:transparent; }
QLabel#Brand { font-size:22px; font-weight:600; color:#f0f4fa; }
QLabel#PageTitle { color:#f0f5fb; font-size:26px; font-weight:600; }
QLabel#Muted { color:#8c9db2; }
QLabel#SectionLabel { color:#a3b1c4; font-size:12px; font-weight:600; }
QLabel#DetailTitle { font-size:17px; font-weight:600; }
QLabel#WelcomeTitle { font-size:24px; font-weight:600; }
QPushButton { background:#1a2637; border:1px solid #2c3a50; border-radius:8px; padding:8px 12px; min-height:18px; font-weight:500; }
QPushButton:hover { background:#26354a; border-color:#51657f; }
QPushButton:focus { border-color:#a8c7fa; }
QPushButton:pressed { background:#30445b; }
QPushButton:checked { background:#263650; border-color:#5876a3; color:#d7e5fa; }
QPushButton::menu-indicator { image:url(:/resources/chevron.svg); width:10px; height:8px; subcontrol-origin:padding; subcontrol-position:right center; right:8px; }
QPushButton#RescanButton { padding-right:24px; }
QPushButton:disabled { color:#5b6b80; background:#152030; border-color:#243043; }
QPushButton#Primary { background:#a8c7fa; color:#15243b; border:1px solid #a8c7fa; font-weight:600; }
QPushButton#Primary:hover { background:#c3d8fa; }
QPushButton#Primary:disabled { background:#344258; color:#8090a6; border-color:#344258; }
QPushButton[navigation="true"] { text-align:left; border:0; background:transparent; padding:12px 14px; color:#97a8bd; }
QPushButton[navigation="true"]:checked { background:#263650; color:#d7e5fa; }
QPushButton[navigation="true"]:hover { background:#1c2b40; }
QPushButton[navigation="true"]:focus { background:#24334b; }
QPushButton#Quiet { background:transparent; border-color:transparent; color:#9db2c9; }
QPushButton#Quiet:hover { background:#1d2b3e; }
QPushButton#Quiet:focus { border-color:#a8c7fa; }
QPushButton#SearchByImage, QPushButton#CloseDetails { padding:0; min-height:0; }
QPushButton#CloseDetails { background:transparent; border-color:transparent; }
QPushButton#CloseDetails:focus { border-color:#a8c7fa; }
QFrame#Search { background:#152130; border:1px solid #33455e; border-radius:10px; }
QLineEdit#SearchBox { border:0; background:transparent; font-size:15px; padding:6px; selection-background-color:#385b91; }
QLineEdit, QPlainTextEdit { background:#101b29; border:1px solid #33445a; border-radius:7px; padding:9px; selection-background-color:#385b91; }
QLineEdit:focus, QPlainTextEdit:focus { border-color:#9fbff1; }
QComboBox { background:#152130; border:1px solid #304055; padding:8px 30px 8px 10px; border-radius:8px; min-width:92px; min-height:18px; }
QComboBox:focus { border-color:#a8c7fa; }
QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:26px; border:0; }
QComboBox::down-arrow { image:url(:/resources/chevron.svg); width:10px; height:8px; }
QComboBox QAbstractItemView { background:#182638; selection-background-color:#304970; padding:7px; }
QComboBox:disabled { color:#687b91; border-color:#273547; }
QListView#Gallery { background:transparent; border:0; outline:0; }
QListWidget#Folders { background:transparent; border:0; outline:0; color:#93a5ba; }
QListWidget#Folders::item { padding:9px 8px; border-radius:6px; }
QListWidget#Folders::item:selected { background:#263650; color:#d7e5fa; }
QListWidget#Folders::item:hover { background:#1a293b; }
QWidget#Inspector { background:#111c2a; border:1px solid #25354a; border-radius:12px; }
QWidget#ReferencePanel { background:#182338; border:1px solid #354768; border-radius:9px; }
QSlider::groove:horizontal { height:4px; background:#304055; border-radius:2px; }
QSlider::handle:horizontal { width:13px; margin:-5px 0; background:#a8c7fa; border-radius:6px; }
QWidget#IndexPanel { background:#182338; border:1px solid #354768; border-radius:9px; }
QProgressBar { background:#233347; border:0; border-radius:3px; height:6px; color:transparent; }
QProgressBar::chunk { background:#a8c7fa; border-radius:3px; }
QScrollBar:vertical { background:transparent; width:9px; margin:3px 0; }
QScrollBar::handle:vertical { background:#34455b; border-radius:4px; min-height:28px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
QScrollBar:horizontal { background:transparent; height:9px; margin:0 3px; }
QScrollBar::handle:horizontal { background:#34455b; border-radius:4px; min-width:28px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }
QScrollArea { border:0; background:transparent; }
QScrollArea > QWidget > QWidget { background:transparent; }
QCheckBox { spacing:9px; padding:5px 0; }
QCheckBox::indicator { width:16px; height:16px; background:#152232; border:1px solid #4b6079; border-radius:4px; }
QCheckBox::indicator:checked { background:#a8c7fa; border-color:#a8c7fa; image:url(:/resources/check.svg); }
QCheckBox:focus { color:#c7dbfb; }
QCheckBox:disabled { color:#73849a; }
QCheckBox::indicator:disabled { background:#101a29; border-color:#304055; }
QToolTip { background:#223246; color:#e4edf7; border:1px solid #486079; padding:6px; }
QMenu { background:#172437; border:1px solid #354961; padding:5px; }
QMenu::item { padding:8px 24px; }
QMenu::item:selected { background:#2b3e5c; }
)");
}
} // namespace piclocate
