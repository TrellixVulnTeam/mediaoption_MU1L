#pragma once
#include <QLabel>
#include <QMouseEvent>
#include <QDebug>
class PlayQlabel :public QLabel
{
	Q_OBJECT;
public:
	explicit PlayQlabel(QWidget *parent = 0);
protected:
	void mousePressEvent(QMouseEvent *ev);
	void mouseReleaseEvent(QMouseEvent* ev);
	void mouseMoveEvent(QMouseEvent* ev);
	
signals:
	void leftclick();

private slots:
	//void sendclick();
};

