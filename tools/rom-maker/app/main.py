import argparse
import json
import os
from pathlib import Path
import re
import sys
import threading

from PySide6.QtCore import QObject, QStandardPaths, QThread, Qt, QTimer, QUrl, Signal, Slot
from PySide6.QtGui import QDesktopServices, QFont, QIcon
from PySide6.QtWidgets import QApplication, QFileDialog, QFrame, QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit, QProgressBar, QPushButton, QSpinBox, QVBoxLayout, QWidget

from runtime import APP_NAME, Cancelled, OUTPUT_FILES, assets_dir, build_rom, self_test


class Worker(QObject):
    message = Signal(str)
    success = Signal(str)
    failure = Signal(str)
    cancelled = Signal()
    finished = Signal()

    def __init__(self, roms, output, jobs):
        super().__init__()
        self.roms, self.output, self.jobs = roms, output, jobs
        self.cancel = threading.Event()

    @Slot()
    def run(self):
        try:
            path = build_rom(self.roms, self.output, self.message.emit, self.cancel, self.jobs)
            self.success.emit(path)
        except Cancelled:
            self.cancelled.emit()
        except Exception as error:
            self.failure.emit(str(error))
        finally:
            self.finished.emit()


class DropPanel(QFrame):
    selected = Signal(list)

    def __init__(self):
        super().__init__()
        self.setAcceptDrops(True)
        self.setObjectName('dropPanel')
        self.setMinimumHeight(104)
        layout = QVBoxLayout(self)
        title = QLabel('Drop your Pac-Land ROM ZIPs or folder here')
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title.setWordWrap(True)
        subtitle = QLabel('Merged, split and non-merged MAME sets are supported')
        subtitle.setObjectName('muted')
        subtitle.setWordWrap(True)
        subtitle.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        layout.addWidget(subtitle)

    def dragEnterEvent(self, event):
        if self.isEnabled() and event.mimeData().hasUrls() and all(u.isLocalFile() for u in event.mimeData().urls()):
            event.acceptProposedAction()

    def dropEvent(self, event):
        self.selected.emit([u.toLocalFile() for u in event.mimeData().urls()])
        event.acceptProposedAction()


class Window(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_NAME)
        self.setWindowIcon(QIcon(str(assets_dir() / 'icon.png')))
        self.resize(720, 700)
        self.setMinimumSize(540, 590)
        self.roms = []
        self.thread = None
        self.worker = None
        self.closing = False
        self.result_path = None
        self.failed = False
        widget = QWidget()
        self.setCentralWidget(widget)
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(26, 24, 26, 24)
        layout.setSpacing(14)
        header = QHBoxLayout()
        icon = QLabel()
        icon.setPixmap(self.windowIcon().pixmap(58, 58))
        header.addWidget(icon)
        headings = QVBoxLayout()
        title = QLabel('Pac-Land MD')
        title.setObjectName('title')
        headings.addWidget(title)
        subtitle = QLabel('ROM Maker  •  Mega Drive / Genesis')
        subtitle.setObjectName('muted')
        headings.addWidget(subtitle)
        header.addLayout(headings, 1)
        layout.addLayout(header)
        description = QLabel('Choose your arcade ROMs and build your cartridge. All build tools are included; ROMs stay on your computer.')
        description.setWordWrap(True)
        layout.addWidget(description)
        self.drop = DropPanel()
        self.drop.selected.connect(self.set_roms)
        layout.addWidget(self.drop)
        choices = QHBoxLayout()
        self.choose = QPushButton('Choose ZIPs…')
        self.choose.clicked.connect(self.choose_roms)
        self.folder = QPushButton('Choose folder…')
        self.folder.clicked.connect(self.choose_folder)
        choices.addWidget(self.choose)
        choices.addWidget(self.folder)
        choices.addStretch()
        layout.addLayout(choices)
        self.selection = QLabel('No arcade ROMs selected')
        self.selection.setObjectName('muted')
        self.selection.setWordWrap(True)
        self.selection.setTextFormat(Qt.TextFormat.PlainText)
        layout.addWidget(self.selection)
        layout.addWidget(QLabel('Save cartridge to'))
        output_row = QHBoxLayout()
        documents = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.DocumentsLocation) or str(Path.home())
        self.output = QLineEdit(str(Path(documents) / 'Pac-Land MD'))
        self.browse = QPushButton('Browse…')
        self.browse.clicked.connect(self.choose_output)
        output_row.addWidget(self.output, 1)
        output_row.addWidget(self.browse)
        layout.addLayout(output_row)
        actions = QHBoxLayout()
        actions.addWidget(QLabel('Build workers'))
        self.jobs = QSpinBox()
        self.jobs.setRange(1, 8)
        self.jobs.setValue(min(8, max(1, (os.cpu_count() or 2) // 2)))
        self.jobs.setToolTip('Fewer workers use less memory. Palette generation can take several minutes.')
        actions.addWidget(self.jobs)
        actions.addStretch()
        self.cancel = QPushButton('Cancel')
        self.cancel.setEnabled(False)
        self.cancel.clicked.connect(self.cancel_build)
        self.build = QPushButton('Build my ROM')
        self.build.setObjectName('primary')
        self.build.setEnabled(False)
        self.build.clicked.connect(self.start_build)
        actions.addWidget(self.cancel)
        actions.addWidget(self.build)
        layout.addLayout(actions)
        self.progress = QProgressBar()
        self.progress.setRange(0, 10)
        self.progress.setValue(0)
        self.progress.setTextVisible(False)
        layout.addWidget(self.progress)
        self.status = QLabel('Ready. No installation or first-run downloads required.')
        self.status.setTextFormat(Qt.TextFormat.PlainText)
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        self.details = QPlainTextEdit()
        self.details.setReadOnly(True)
        self.details.setMaximumBlockCount(6000)
        self.details.setPlaceholderText('Build progress and diagnostic details will appear here.')
        self.details.setMinimumHeight(110)
        layout.addWidget(self.details, 1)
        footer = QHBoxLayout()
        self.save_log = QPushButton('Save build log…')
        self.save_log.clicked.connect(self.export_log)
        self.open = QPushButton('Open output folder')
        self.open.setEnabled(False)
        self.open.clicked.connect(lambda: QDesktopServices.openUrl(QUrl.fromLocalFile(str(Path(self.result_path).parent))))
        footer.addWidget(self.save_log)
        footer.addStretch()
        footer.addWidget(self.open)
        layout.addLayout(footer)
        self.setStyleSheet('''
QWidget { background: #171b24; color: #eff1f5; font-size: 13px; }
QLabel#title { font-size: 27px; font-weight: 700; }
QLabel#muted { color: #aeb7c8; }
QFrame#dropPanel { background: #202633; border: 1px dashed #67738b; border-radius: 12px; }
QFrame#dropPanel QLabel { background: transparent; border: none; }
QPushButton { background: #2c3545; border: 1px solid #465167; padding: 9px 13px; border-radius: 7px; }
QPushButton:hover { background: #3b475d; }
QPushButton:disabled { color: #727d91; border-color: #30394a; background: #222a37; }
QPushButton#primary { background: #f9df51; border-color: #f9df51; color: #171b24; font-weight: 700; }
QPushButton#primary:disabled { background: #625d39; border-color: #625d39; color: #a6a085; }
QLineEdit, QPlainTextEdit, QSpinBox { background: #11151d; border: 1px solid #3e495f; border-radius: 6px; padding: 7px; selection-background-color: #766820; }
QProgressBar { background: #293243; border: none; border-radius: 3px; max-height: 6px; }
QProgressBar::chunk { background: #f9df51; border-radius: 3px; }
''')

    def set_roms(self, paths):
        paths = list(dict.fromkeys(str(Path(p).expanduser().resolve()) for p in paths))
        if not paths:
            return
        if any(not Path(p).is_dir() and (not Path(p).is_file() or Path(p).suffix.lower() != '.zip') for p in paths):
            QMessageBox.warning(self, APP_NAME, 'Choose existing ZIP files or a folder containing your arcade ROM ZIPs.')
            return
        self.roms = paths
        self.selection.setText('\n'.join(Path(p).name for p in paths))
        self.selection.setToolTip('\n'.join(paths))
        self.build.setEnabled(True)

    def choose_roms(self):
        paths, _ = QFileDialog.getOpenFileNames(self, 'Choose your arcade ROM ZIPs', '', 'ROM ZIPs (*.zip *.ZIP)')
        self.set_roms(paths)

    def choose_folder(self):
        path = QFileDialog.getExistingDirectory(self, 'Choose your arcade ROM folder')
        if path:
            self.set_roms([path])

    def choose_output(self):
        path = QFileDialog.getExistingDirectory(self, 'Choose output folder', self.output.text())
        if path:
            self.output.setText(path)

    def busy(self, value):
        for control in (self.choose, self.folder, self.drop, self.output, self.browse, self.jobs):
            control.setEnabled(not value)
        self.build.setEnabled(not value and bool(self.roms))
        self.cancel.setEnabled(value)

    def start_build(self):
        if self.thread:
            return
        if not self.output.text().strip():
            QMessageBox.warning(self, APP_NAME, 'Choose an output folder first.')
            return
        output = Path(self.output.text().strip()).expanduser().resolve()
        if any((output / name).exists() for name in OUTPUT_FILES):
            choice = QMessageBox.question(self, 'Replace previous build?', 'This folder already contains build output. Replace it after the new build succeeds?', QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
            if choice != QMessageBox.StandardButton.Yes:
                return
        self.details.clear()
        self.result_path = None
        self.failed = False
        self.open.setEnabled(False)
        self.progress.setRange(0, 0)
        self.status.setText('Preparing bundled tools. The first build can take several minutes.')
        self.busy(True)
        self.thread = QThread(self)
        self.worker = Worker(self.roms, str(output), self.jobs.value())
        self.worker.moveToThread(self.thread)
        self.thread.started.connect(self.worker.run)
        self.worker.message.connect(self.on_message)
        self.worker.success.connect(self.on_success)
        self.worker.failure.connect(self.on_failure)
        self.worker.cancelled.connect(self.on_cancelled)
        self.worker.finished.connect(self.thread.quit)
        self.worker.finished.connect(self.worker.deleteLater)
        self.thread.finished.connect(self.on_finished)
        self.thread.finished.connect(self.thread.deleteLater)
        self.thread.start()

    @Slot(str)
    def on_message(self, text):
        self.details.appendPlainText(text)
        match = re.search(r'\[(\d+)/10\]', text)
        if match:
            self.progress.setRange(0, 10)
            self.progress.setValue(max(0, int(match[1]) - 1))
            self.status.setText(text.strip())
        elif text.startswith('Preparing a temporary'):
            self.status.setText('Preparing temporary build tools…')

    @Slot(str)
    def on_success(self, path):
        self.result_path = path
        self.progress.setRange(0, 10)
        self.progress.setValue(10)
        self.status.setText('Your cartridge is ready: ' + path)
        self.open.setEnabled(True)

    @Slot(str)
    def on_failure(self, error):
        self.failed = True
        self.progress.setRange(0, 10)
        self.status.setText('Build failed. See the details or save the log for support.')
        self.details.appendPlainText('\nERROR: ' + error)

    @Slot()
    def on_cancelled(self):
        self.progress.setRange(0, 10)
        self.progress.setValue(0)
        self.status.setText('Build cancelled. Temporary files removed.')

    @Slot()
    def on_finished(self):
        self.thread = None
        self.worker = None
        self.busy(False)
        if self.closing:
            self.close()

    def cancel_build(self):
        if self.worker:
            self.worker.cancel.set()
            self.cancel.setEnabled(False)
            self.status.setText('Cancelling and removing temporary files…')

    def export_log(self):
        path, _ = QFileDialog.getSaveFileName(self, 'Save build log', 'Pac-Land-MD-build.log', 'Log files (*.log)')
        if path:
            try:
                Path(path).write_text(self.details.toPlainText(), encoding='utf-8')
            except OSError as error:
                QMessageBox.warning(self, APP_NAME, str(error))

    def closeEvent(self, event):
        if self.thread:
            if not self.closing:
                choice = QMessageBox.question(self, APP_NAME, 'Cancel the current build and close?', QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
                if choice != QMessageBox.StandardButton.Yes:
                    event.ignore()
                    return
                self.closing = True
                self.cancel_build()
            event.ignore()
        else:
            event.accept()


def main():
    parser = argparse.ArgumentParser(description=APP_NAME)
    parser.add_argument('roms', nargs='*')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--report')
    parser.add_argument('--smoke-ui', action='store_true')
    args = parser.parse_args()
    app = QApplication([sys.argv[0]])
    app.setApplicationName(APP_NAME)
    app.setOrganizationName('PacLandMD')
    app.setFont(QFont('Arial', 10))
    if args.self_test:
        try:
            result = self_test(args.report)
            window = Window()
            window.show()
            app.processEvents()
            window.close()
            if sys.stdout:
                print(json.dumps(result, indent=2))
            return 0
        except Exception as error:
            result = {'ok': False, 'error': str(error)}
            if args.report:
                Path(args.report).write_text(json.dumps(result, indent=2) + '\n')
            if sys.stderr:
                print(str(error), file=sys.stderr)
            return 1
    window = Window()
    if args.roms:
        window.set_roms(args.roms)
    window.show()
    if args.smoke_ui:
        QTimer.singleShot(1200, app.quit)
    return app.exec()


if __name__ == '__main__':
    raise SystemExit(main())
