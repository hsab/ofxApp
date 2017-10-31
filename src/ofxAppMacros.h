//
//  ofxAppMacros.h
//  BaseApp
//
//  Created by Oriol Ferrer Mesi√† on 3/8/16.
//
//

#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////

//this is all to achieve variable includes given a user specified macro name for the app
//http://stackoverflow.com/questions/32066204/construct-path-for-include-directive-with-macro
//http://stackoverflow.com/questions/1489932/how-to-concatenate-twice-with-the-c-preprocessor-and-expand-a-macro-as-in-arg
//this is to directly include your ofxAppColorsBasic.h, AppGlobalsBasic.h, ofxAppFonts.h subclasses.

#define OFX_APP_IDENT(x) x
#define OFX_APP_XSTR(x) #x
#define OFX_APP_STR(x) OFX_APP_XSTR(x)
#define OFX_APP_INCLUDE(x,y) OFX_APP_STR(OFX_APP_IDENT(x)OFX_APP_IDENT(y))

//lots of indirection for this to work...
#define OFX_APP_PASTER(x,y) x ## y
#define OFX_APP_EVALUATOR(x,y)  OFX_APP_PASTER(x,y)
#define OFX_APP_CLASS_NAME(class) OFX_APP_EVALUATOR(OFX_APP_NAME,class)

#define OFX_COLORS_FILENAME Colors.h
#define OFX_GLOBALS_FILENAME Globals.h

////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef TARGET_WIN32
const string FILE_ACCES_ICON = "[!]";
#else
const string FILE_ACCES_ICON = "💾";
#endif


#define GLOB											((OFX_APP_CLASS_NAME(Globals)&)(*ofxApp::get().globals()))
#define G_COL											ofxApp::get().colors()
#define G_COLOR											ofxApp::get().colors()

#define G_TEX(name)										ofxApp::get().textures().getTexture(name)

#define G_FONT(name)									ofxApp::get().fonts().getFont(name)
#define G_FONT_MONO										ofxApp::get().fonts().getMonoFont()
#define G_FONT_MONO_BOLD								ofxApp::get().fonts().getMonoBoldFont()

#define G_FS2()											ofxApp::get().fonts().getFontStash2()
#define G_FSTYLE(S)										ofxApp::get().fonts().getFontStyle(S)




#define OFXAPP_REPORT(alertID,msg,severity)						ofxApp::get().errorReporter().send(alertID,msg,severity)
#define OFXAPP_REPORT_FILE(alertID,msg,severity,fileToSend)		ofxApp::get().errorReporter().send(alertID,msg,severity,fileToSend)

#define OFXAPP_ANALYTICS()										ofxApp::get().analytics()

// Logging
//for in-class methods only - will throw compiler error in static methods or classless functions
#define LOGV 										ofLogNotice(SUPERLOG_TYPE_NAME)
#define LOGN 										ofLogNotice(SUPERLOG_TYPE_NAME)
#define LOGW 										ofLogWarning(SUPERLOG_TYPE_NAME)
#define LOGE 										ofLogError(SUPERLOG_TYPE_NAME)
#define LOGF 										ofLogFatalError(SUPERLOG_TYPE_NAME)

