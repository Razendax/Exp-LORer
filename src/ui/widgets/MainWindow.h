#pragma once

#include <QMainWindow>

// Empty application shell; populated with real views once use cases exist.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};
