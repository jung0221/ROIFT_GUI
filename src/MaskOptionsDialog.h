#pragma once

#include <QDialog>
#include <QSpinBox>
#include <QPushButton>
#include <QSlider>

class MaskOptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit MaskOptionsDialog(QWidget *parent=nullptr);
    int maskMode() const { return m_mode; } // 0 idle,1 draw,2 erase
    int brushRadius() const { return m_brushRadius->value(); }

signals:
    void modeChanged(int mode);
    void loadMaskRequested();
    void saveMaskRequested();
    void cleanRequested();
    void brushRadiusChanged(int r);
    void maskOpacityChanged(int percent);

private:
    int m_mode = 0;
    QSpinBox *m_brushRadius;
    QPushButton *m_btnDraw;
    QPushButton *m_btnErase;
    QSlider *m_opacitySlider;
};
