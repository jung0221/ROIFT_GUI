#pragma once

#include <QDialog>
#include <QSpinBox>
#include <QPushButton>

class SeedOptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SeedOptionsDialog(QWidget *parent=nullptr);
    int seedMode() const { return m_mode; } // 0 idle, 1 draw, 2 erase
    int brushRadius() const { return m_brushRadius->value(); }

signals:
    void modeChanged(int mode);
    void cleared();
    void saveRequested();
    void loadRequested();
    void brushRadiusChanged(int r);

private:
    int m_mode = 1;
    QSpinBox *m_brushRadius;
    QPushButton *m_btnDraw;
    QPushButton *m_btnErase;
};
