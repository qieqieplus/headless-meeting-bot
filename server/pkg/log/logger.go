package log

import (
	"os"

	"github.com/sirupsen/logrus"
)

var Logger *logrus.Logger

func Init(level string) {
	Logger = logrus.New()
	Logger.SetOutput(os.Stdout)
	Logger.SetFormatter(&logrus.JSONFormatter{
		TimestampFormat: "2006-01-02T15:04:05.000Z07:00",
	})

	logLevel, err := logrus.ParseLevel(level)
	if err != nil {
		logLevel = logrus.InfoLevel
	}
	Logger.SetLevel(logLevel)
}

// Convenience functions
func Debug(args ...interface{}) {
	if Logger != nil {
		Logger.Debug(args...)
	}
}

func Debugf(format string, args ...interface{}) {
	if Logger != nil {
		Logger.Debugf(format, args...)
	}
}

func Info(args ...interface{}) {
	if Logger != nil {
		Logger.Info(args...)
	}
}

func Infof(format string, args ...interface{}) {
	if Logger != nil {
		Logger.Infof(format, args...)
	}
}

func Warn(args ...interface{}) {
	if Logger != nil {
		Logger.Warn(args...)
	}
}

func Warnf(format string, args ...interface{}) {
	if Logger != nil {
		Logger.Warnf(format, args...)
	}
}

func Error(args ...interface{}) {
	if Logger != nil {
		Logger.Error(args...)
	}
}

func Errorf(format string, args ...interface{}) {
	if Logger != nil {
		Logger.Errorf(format, args...)
	}
}

func Fatal(args ...interface{}) {
	if Logger != nil {
		Logger.Fatal(args...)
	}
}

func Fatalf(format string, args ...interface{}) {
	if Logger != nil {
		Logger.Fatalf(format, args...)
	}
}
