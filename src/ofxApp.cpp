//
//  ofxApp.cpp
//  BaseApp
//
//  Created by Oriol Ferrer Mesi√† on 3/8/16.
//
//

#include "ofxApp.h"
#include "ofxThreadSafeLog.h"
#include "TexturedObjectStats.h"
#include "ofxAppUtils.h"

//how to get the app from the ofxApp namespace
namespace ofxApp{
	App& get(){
		return App::one();
	}
}

using namespace ofxApp;

App::App() {
	cout << "ofxApp::App()\n";
	fontStorage = new ofxAppFonts();
	#ifdef OFX_APP_NONAME
	globalsStorage = new ofxAppGlobalsBasic;
	#else
	globalsStorage = new OFX_APP_CLASS_NAME(Globals);
	#endif
}

void App::setup(ofxAppDelegate * delegate){
	map<string,ofxApp::ParseFunctions> emptyLambas;
	setup(emptyLambas, delegate);
}


void App::setup(const map<string,ofxApp::ParseFunctions> & cfgs, ofxAppDelegate * delegate){

	ofLogNotice("ofxApp") << "setup()";

	//create pid file
	bool pidFileFound = ofFile::doesFileExist(pidFileName);
	ofLogNotice("ofxApp") << "Create PID file at " << pidFileName;
	ofFile pid;
	pid.open(pidFileName, ofFile::WriteOnly, true);
	pid.close();

	if(!this->delegate){
		contentCfgs = cfgs;
		this->delegate = delegate;
		if(!hasLoadedSettings) loadSettings();
		setupContentData();
		setupLogging();
		if(pidFileFound){
			ofLogNotice("ofxApp") << "Found '" << pidFileName << "' file! The App did not exit cleanly when it was last run.";
		}
		printOpenGlInfo();
		setupRemoteUI();
		setupErrorReporting();
		setupGoogleAnalytics();
		printSettingsFile();
		fonts().setup();
		if(getBool("Logging/useFontStash")){ //set a nice font for the on screen logger if using fontstash
			ofxSuperLog::getLogger()->setFont(&(fonts().getMonoBoldFont()), getInt("Logging/fontSize"));
		}
		loadModulesSettings();
		if(timeSampleOfxApp) TS_START_NIF("ofxApp Setup");
		setupTimeMeasurements();
		setupTextureLoader();
		setupWindow();
		setupOF();
		ofxSimpleHttp::createSslContext();
		setupStateMachine();
		appState.setState(State::SETUP_OFXAPP_INTERNALS);
		setupListeners();
		setupGlobalParameters();
		textures().setup();
		setupTuio();

		if(timeSampleOfxApp) TS_START_NIF("ofxApp Load Static Textures");

		bool maintenance = getBool("App/MaintenanceMode/enabled");
		if(!maintenance){
			appState.setState(State::SETUP_DELEGATE_B4_CONTENT_LOAD); //start loading content
		}else{
			appState.setState(State::MAINTENANCE);
		}
	}else{
		ofxApp::utils::terminateApp("ofxApp", "Trying to setup() ofxApp a second time!");
	}
}


App::~App(){
	//cout << (*loggerStorage).use_count() << endl;
	ofLogNotice("ofxApp")<< "~ofxApp()";
}


void App::setupContentData() {
	ofLogNotice("ofxApp") << "setupContentData()";
	for (auto & cfg : contentCfgs) {
		contentStorage[cfg.first] = new ofxAppContent();
		requestedContent.push_back(cfg.first);
	}
	if(requestedContent.size()){
		currentContentID = requestedContent[0];
	}
}


void App::setupOF(){
	ofLogNotice("ofxApp") << "setupOF()";
	ofSetFrameRate(getInt("App/frameRate"));
	ofBackground(22);
	dt = 1.0f / ofGetTargetFrameRate();

	bool showMouse = getBool("App/showMouse");
	if(showMouse) ofShowCursor();
	else ofHideCursor();

	setMouseEvents(getBool("App/enableMouse"));
}

void App::printOpenGlInfo(){
	logBanner("GL Info");
	ofxApp::utils::logParagraph("ofxApp", OF_LOG_NOTICE, ofxApp::utils::getGlInfo());

	auto exts = ofGLSupportedExtensions();
	if(exts.size()){
		logBanner("Available GL Extensions");
		for(auto & ext: exts){
			ofLogNotice("ofxApp") << ext;
		}
	}
}

void App::setMouseEvents(bool enabled){
	
	ofLogNotice("ofxApp") << "setMouseEvents()";
	if(enabled){
		if(!ofEvents().mousePressed.isEnabled()){
			ofEvents().mousePressed.enable();
			ofEvents().mouseReleased.enable();
			ofEvents().mouseDragged.enable();
			ofLogWarning("ofxApp") << "Enabled Mouse Events";
		}
	}else{
		if(ofEvents().mousePressed.isEnabled()){
			ofEvents().mousePressed.disable();
			ofEvents().mouseReleased.disable();
			ofEvents().mouseDragged.disable();
			ofLogWarning("ofxApp") << "Disabled Mouse Events";
		}
	}
}


void App::setupErrorReporting(){
	
	ofLogNotice("ofxApp") << "setupErrorReporting()";
	
	reportErrors = getBool("ErrorReporting/enabled");
	int port = getInt("ErrorReporting/port");
	string host = getString("ErrorReporting/host");
	
	vector<string> emails;
	
	if(settings().exists("ErrorReporting/emails")){ //see if its a list of emails
		ofxJSON emailsJson = settings().getJson("ErrorReporting/emails");
		if(emailsJson.isArray()){
			for( Json::ValueIterator itr = emailsJson.begin() ; itr != emailsJson.end() ; itr++ ) {
				string email = (*itr).asString();
				emails.push_back(email);
			}
		}
	}else{ //otherwise its a single email
		string email = getString("ErrorReporting/email");
		emails.push_back(email);
	}

	bool attachGitStatus = getBool("ErrorReporting/reportGitStatus");
	
	errorReporterObj.setup(	host, port, emails, reportErrors,
						   	RUI_GET_INSTANCE()->getComputerName(),
						   	RUI_GET_INSTANCE()->getComputerIP(),
						   	RUI_GET_INSTANCE()->getBinaryName(),
							attachGitStatus
						   );
}


void App::setupWindow(){
	
	ofLogNotice("ofxApp") << "setupWindow()";
	ofxScreenSetup::ScreenMode mode = ofxScreenSetup::ScreenMode((int)getInt("App/window/windowMode"));
	screenSetup.setup(getInt("App/window/customWidth"), getInt("App/window/customHeight"), mode);
	

	bool customPosition = getBool("App/window/customWindowPosition");
	int customX = getInt("App/window/customPositionX");
	int customY = getInt("App/window/customPositionY");
	if(customPosition){
		ofLogNotice("ofxApp") << "Setting a custom window position [" << customX << " , " << customY << "]";
		ofSetWindowPosition(customX, customY);
	}

	//setup mullions user settings
	bool mullionsVisible = getBool("App/mullions/visibleAtStartup");
	mullions.setup(getInt("App/mullions/numX"), getInt("App/mullions/numY"));
	if(mullionsVisible) mullions.enable();
	else mullions.disable();
	
	//trying to get the window to "show up" in the 1st frame - to show terminateApp() in the 1st frame
	GLFWwindow* glfwWindow = (GLFWwindow*)ofGetWindowPtr()->getWindowContext();
	glfwShowWindow(glfwWindow);
	//ofGetWindowPtr()->setFullscreen(true);
	//ofSetWindowPosition(0,0);
	ofGetWindowPtr()->makeCurrent();
	ofGetGLRenderer()->startRender();
	ofGetGLRenderer()->setupScreen();
	ofGetGLRenderer()->finishRender();
//	ofGetWindowPtr()->update();
//	ofGetWindowPtr()->draw();
	ofGetMainLoop()->pollEvents();
	windowIsSetup = true;
}


void App::setupListeners(){

	ofLogNotice("ofxApp") << "setupListeners()";
	ofAddListener(ofEvents().update, this, &App::update);
	ofAddListener(ofEvents().exit, this, &App::exit, OF_EVENT_ORDER_AFTER_APP + 100); //last thing hopefully!
	//listen to content manager state changes
	for(auto c : contentStorage){
		ofAddListener(c.second->eventStateChanged, this, &App::onContentManagerStateChanged);
	}

	ofAddListener(ofEvents().keyPressed, this, &App::onKeyPressed);
	ofAddListener(ofEvents().draw, this, &App::draw, OF_EVENT_ORDER_AFTER_APP);
	ofAddListener(textures().eventAllTexturesLoaded, this, &App::onStaticTexturesLoaded);

	ofAddListener(screenSetup.setupChanged, this, &App::screenSetupChanged);

}


void App::setupGoogleAnalytics(){
	
	ofLogNotice("ofxApp") << "setupGoogleAnalytics()";
	bool enabled = getBool("GoogleAnalytics/enabled");
	string googleID = getString("GoogleAnalytics/googleID");
	string appName = getString("GoogleAnalytics/appName");
	string appVersion = getString("GoogleAnalytics/appVersion");
	string appID = getString("GoogleAnalytics/appID");
	string appInstallerID = getString("GoogleAnalytics/appInstallerID");
	bool verbose = getBool("GoogleAnalytics/verbose");
	bool doBench = getBool("GoogleAnalytics/sendBenchmark");
	bool randUUID = getBool("GoogleAnalytics/randomizedUUID");
	int maxRequestsPerSession = getInt("GoogleAnalytics/maxRequestsPerSession");
	float sendDataIntervalSec = getFloat("GoogleAnalytics/sendDataInterval");
	bool shouldReportFramerate = getBool("GoogleAnalytics/shouldReportFramerate");
	float framerateReportInterval = getFloat("GoogleAnalytics/framerateReportInterval");
	
	gAnalytics = new ofxGoogleAnalytics();
	gAnalytics->setVerbose(verbose);
	gAnalytics->setEnabled(enabled);
	gAnalytics->setRandomizeUUID(randUUID);
	gAnalytics->setSendSimpleBenchmarks(doBench);
	gAnalytics->setShouldReportFramerates(shouldReportFramerate);
	gAnalytics->setMaxRequestsPerSession(maxRequestsPerSession);
	gAnalytics->setSendToGoogleInterval(sendDataIntervalSec);
	gAnalytics->setFramerateReportInterval(framerateReportInterval);
	gAnalytics->setUserID("ofxApp"); //todo
	
	gAnalytics->setup( googleID, appName, appVersion, appID, appInstallerID );
}


void App::setupStateMachine(){

	ofLogNotice("ofxApp") << "setupStateMachine()";
	//listen to state machine changes
	ofAddListener(appState.eventStateChanged, this, &App::onStateChanged);
	ofAddListener(appState.eventStateError, this, &App::onStateError);
	ofAddListener(appState.eventDraw, this, &App::onDrawLoadingScreenStatus);

	string boldFontPath = ofxAppFonts::getMonoBoldFontPath();
	ofxApp::utils::assertFileExists(boldFontPath);
	appState.setup(boldFontPath, "", ofColor(0,0,0,0), ofColor::white);
	float dark = 0.25;

	//TODO some color consitency here please? or at least uniformity
	//this creates strings for each of the ENUM states
	appState.setNameAndBarColorForState(State::SETUP_OFXAPP_INTERNALS, toString(State::SETUP_OFXAPP_INTERNALS), ofColor(0,0,255), ofColor(0,0,128));
	appState.setNameAndBarColorForState(State::SETUP_DELEGATE_B4_CONTENT_LOAD, toString(Phase::WILL_LOAD_CONTENT), ofColor::magenta, ofColor::magenta * dark);
	appState.setNameAndBarColorForState(State::LOAD_STATIC_TEXTURES, toString(State::LOAD_STATIC_TEXTURES), ofColor::darkorange, ofColor::darkorange * dark);
	appState.setNameAndBarColorForState(State::LOAD_JSON_CONTENT, toString(State::LOAD_JSON_CONTENT), ofColor::forestGreen, ofColor::forestGreen * dark);
	appState.setNameAndBarColorForState(State::LOAD_JSON_CONTENT_FAILED, toString(State::LOAD_JSON_CONTENT_FAILED), ofColor::crimson, ofColor::crimson * dark);
	appState.setNameAndBarColorForState(State::DELIVER_CONTENT_LOAD_RESULTS, toString(Phase::DID_DELIVER_CONTENT), ofColor::blueViolet, ofColor::blueViolet * dark);
	appState.setNameAndBarColorForState(State::SETUP_DELEGATE_B4_RUNNING, toString(Phase::WILL_BEGIN_RUNNING), ofColor::mediumAquaMarine, ofColor::mediumAquaMarine * dark);
	appState.setNameAndBarColorForState(State::RUNNING, toString(State::RUNNING), ofColor::white, ofColor::grey);
	appState.setNameAndBarColorForState(State::MAINTENANCE, toString(State::MAINTENANCE), ofColor::white, ofColor::grey);
	appState.setNameAndBarColorForState(State::DEVELOPER_REQUESTED_ERROR_SCREEN, toString(State::DEVELOPER_REQUESTED_ERROR_SCREEN), ofColor::white, ofColor::grey);
}


void App::startLoadingStaticAssets(){
	ofLogNotice("ofxApp") << "startLoadingStaticAssets()";
	string texturesPath = getString("StaticAssets/textures");
	if(texturesPath.size()){
		ofxApp::utils::assertFileExists(texturesPath);
		if(settingExists("StaticAssets/forceMipMaps")){
			textures().setForceMipmaps(getBool("StaticAssets/forceMipMaps"));
		}
		textures().loadTexturesInDir(texturesPath, getInt("App/maxThreads"));
	}else{
		ofLogWarning("ofxApp") << "App doesnt want to load static Assets!";
		onStaticTexturesLoaded();
	}
}


void App::setupTextureLoader(){

	int maxThreads = getInt("TextureLoader/maxNumberSimulataneousLoads");
	float mipmapBias = getFloat("TextureLoader/textureLodBias");
	float maxMsPerFrame = getFloat("TextureLoader/maxTimeSpentLoadingPerFrameMs");
	int scanLinesPerLoop = getInt("TextureLoader/scanlinesPerLoop");
	int maxReq = getInt("TextureLoader/maxLoadRequestsPerFrame");
	ofLogNotice("ofxApp") << "setupTextureLoader() [maxThreads:" << maxThreads << " maxMsPerFrame:" << maxMsPerFrame <<
	" nScanLinesPerLoop:" << scanLinesPerLoop << " mipmapBias:" << mipmapBias << " maxReqPerFrame:" << maxReq << "]";
	ProgressiveTextureLoadQueue * q = ProgressiveTextureLoadQueue::instance();
	q->setMaxThreads( maxThreads ); //N threads loading images in the bg
	q->setTexLodBias( mipmapBias ); //MipMap sharpness
	q->setTargetTimePerFrame( maxMsPerFrame );	//spend at most 'x' milis loading textures per frame
	q->setScanlinesPerLoop( scanLinesPerLoop );
	q->setMaximumRequestsPerFrame( maxReq );

}


void App::setupGlobalParameters(){
	globals()->ofxAppGlobalsBasic::setupRemoteUIParams();
	RUI_NEW_GROUP(string(OFX_APP_STR(OFX_APP_NAME)) + string(" Globals"));
	globals()->setupRemoteUIParams();
	RUI_NEW_GROUP(string(OFX_APP_STR(OFX_APP_NAME)) + string(" Colors"));
	colors().ofxAppColorsBasic::setupRemoteUIParams();
	colors().setupRemoteUIParams();
}


void App::loadSettings(){

	ofxApp::utils::assertFileExists(settingsFile);

	ofLogNotice("ofxApp") << "loadSettings() from \"" << settingsFile << "\"";
	bool ok = settings().load(ofToDataPath(settingsFile, true));
	hasLoadedSettings = true;
	if(!ok){
		ofxApp::utils::terminateApp("ofxApp", "Could not load settings from \"" + ofToDataPath(settingsFile, true) + "\"");
	}
	startupScreenViewport.x = getFloat("App/startupScreenViewport/x", 0);
	startupScreenViewport.y = getFloat("App/startupScreenViewport/y", 0);
	startupScreenViewport.width = getFloat("App/startupScreenViewport/w", 0);
	startupScreenViewport.height = getFloat("App/startupScreenViewport/h", 0);
}


void App::printSettingsFile(){

	ofBuffer jsonFile = ofBufferFromFile(settingsFile, false);

	logBanner("Loaded ofxApp Settings - JSON Contents follow :");
	vector<string> jsonLines = ofSplitString(jsonFile.getText(), "\n");
	#ifdef TARGET_WIN32
	ofLogNotice("ofxApp") << " %%%%%% AppSettings.json %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%";
	for (auto & l : jsonLines) {
		ofLogNotice("ofxApp") << " % " << l;
	}
	ofLogNotice("ofxApp") << " %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%";
	#else
	ofLogNotice("ofxApp") << " ╔═════╣ ofxAppSettings.json ╠═════════════════════════════════════════════════════════════════════════════════";
	for (auto & l : jsonLines) {
		ofLogNotice("ofxApp") << " ║ " << l;
	}
	ofLogNotice("ofxApp") << " ╚══════════════════════════════════════════════════════════════════════════════════════════════════════════";
	#endif	
}


void App::saveSettings(){
	ofLogNotice("ofxApp") << "saveSettings() to " << settingsFile;
	settings().save(ofToDataPath(settingsFile, true));
	string settingsString = settings().getAsJsonString();
	logBanner("Saved Settings: \n" + settingsString + "\n");
}


void App::setupApp(){

	ofLogNotice("ofxApp") << "setupApp()";
	RUI_NEW_GROUP("APP");
	showMouse = getBool("App/showMouse");
	RUI_SHARE_PARAM(showMouse);
	enableMouse = getBool("App/enableMouse");
	RUI_SHARE_PARAM(enableMouse);
	RUI_PUSH_TO_CLIENT();
	//RUI_LOAD_FROM_XML();
	setMouseEvents(enableMouse);
	ofBackground(colorsStorage.bgColor);
}


void App::setupLogging(){

	ofLogNotice("ofxApp") << "setupLogging()";
	if(getBool("Logging/deleteOldLogs")){
		ofxSuperLog::clearOldLogs(LogsDir, getInt("Logging/logExpirationInDays"));
	}
	bool logToConsole = getBool("Logging/toConsole");
	bool logToScreen = getBool("Logging/toScreen");
	ofSetLogLevel(ofLogLevel(getInt("Logging/logLevel")));
	//lets keep a ref to the logger counter around so that we can control when it gets deleted
	
	loggerStorage = new ofPtr<ofxSuperLog>(); //note this 2* madness is to avoid the logger being delete b4 the app is finished logging
	*loggerStorage = ofxSuperLog::getLogger(logToConsole, logToScreen, LogsDir);
	ofSetLoggerChannel(*loggerStorage);
	bool visible = getBool("Logging/visibleAtStartup");
	(*loggerStorage)->setScreenLoggingEnabled(visible);
	(*loggerStorage)->setMaximized(true);
	(*loggerStorage)->setMaxNumLogLines(getInt("Logging/maxScreenLines"));
	(*loggerStorage)->setUseScreenColors(true);
	(*loggerStorage)->setSyncronizedLogging(getBool("Logging/syncronizedLogging"));
	(*loggerStorage)->getDisplayLogger().setDisplayLogTimes(getBool("Logging/displayLogTimes"));
	
	float panelW = getFloat("Logging/screenLogPanelWidth");
	ofxSuperLog::getLogger()->setDisplayWidth(panelW);

	//asset manager uses this separate logger to create an "asset report"  file after every launch
 	//stating status of every downloaded asset (ie missing sha1, sha1 missmatch, etc)
	ofxThreadSafeLog::one()->setPrintToConsole(getBool("Logging/ThreadSafeLog/alsoPrintToConsole"));
}


void App::setupRemoteUI(){

	ofLogNotice("ofxApp") << "setupRemoteUI()";
	RUI_SET_CONFIGS_DIR(configsDir);
	RUI_GET_INSTANCE()->setUiColumnWidth(getInt("RemoteUI/columnWidth", 280));
	RUI_GET_INSTANCE()->setBuiltInUiScale(getFloat("RemoteUI/uiScale", 1.0));
	bool useFontStash = getBool("RemoteUI/useFontStash");
	if(useFontStash){
		string fontFile = getString("RemoteUI/fontFile");
		ofxApp::utils::assertFileExists(fontFile);
		if(!ofIsGLProgrammableRenderer()){
			ofLogWarning("ofxApp") << "Using ofxFontStash2 for RemoteUI as we are using GL2!";
			RUI_GET_INSTANCE()->drawUiWithFontStash(fontFile, getInt("RemoteUI/fontSize", 15));
		}else{
			ofLogWarning("ofxApp") << "Using ofxFontStash2 for RemoteUI as we are using GL3+!";
			RUI_GET_INSTANCE()->drawUiWithFontStash2(fontFile, getInt("RemoteUI/fontSize", 15));
		}
	}
	bool ruiSaveOnQuit = getBool("RemoteUI/saveSettingsOnExit");
	RUI_GET_INSTANCE()->setSaveToXMLOnExit(ruiSaveOnQuit);

	bool autoBackupsWhenSaving = getBool("RemoteUI/automaticBackupsOnSave");
	RUI_GET_INSTANCE()->setAutomaticBackupsEnabled(autoBackupsWhenSaving);

	bool drawNotifications = getBool("RemoteUI/drawOnScreenNotifications");
	RUI_GET_INSTANCE()->setDrawsNotificationsAutomaticallly(drawNotifications);

	float notifScreenTime = getFloat("RemoteUI/notificationsScreenTime");
	RUI_GET_INSTANCE()->setNotificationScreenTime(notifScreenTime);

	float logNotifScreenTime = getFloat("RemoteUI/logNotificationsScreenTime");
	RUI_GET_INSTANCE()->setLogNotificationScreenTime(logNotifScreenTime);

	ofLogNotice("ofxApp") << "RemoteUI will save settings on quit: " << ruiSaveOnQuit;
	RUI_GET_INSTANCE()->setShowUIDuringEdits(getBool("RemoteUI/showUiDuringEdits"));

	ofAddListener(RUI_GET_OF_EVENT(), this, &App::onRemoteUINotification);
	RUI_SETUP();

	bool enabled = getBool("RemoteUI/enabled");
	if(!enabled){
		ofLogWarning("ofxApp") << "Disabling ofxRemoteUI as specified in json settings!";
		RUI_GET_INSTANCE()->setEnabled(enabled);
	}
}


void App::loadModulesSettings(){

	std::pair<string,string> credentials;
	credentials.first = getString("Downloads/credentials/username");
	credentials.second = getString("Downloads/credentials/password");

	ofxSimpleHttp::ProxyConfig proxyCfg;
	proxyCfg.useProxy = getBool("Downloads/proxy/useProxy");
	proxyCfg.host = getString("Downloads/proxy/proxyHost");
	proxyCfg.port = getInt("Downloads/proxy/proxyPort");
	proxyCfg.login = getString("Downloads/proxy/proxyUser");
	proxyCfg.password = getString("Downloads/proxy/proxyPassword");

	assetDownloadPolicy.fileMissing = getBool("Content/AssetDownloadPolicy/fileMissing");
	assetDownloadPolicy.fileTooSmall = getBool("Content/AssetDownloadPolicy/fileTooSmall");
	assetDownloadPolicy.fileExistsAndNoSha1Provided = getBool("Content/AssetDownloadPolicy/fileExistsAndNoSha1Provided");
	assetDownloadPolicy.fileExistsAndProvidedSha1Missmatch = getBool("Content/AssetDownloadPolicy/fileExistsAndProvidedSha1Missmatch");
	assetDownloadPolicy.fileExistsAndProvidedSha1Match = getBool("Content/AssetDownloadPolicy/fileExistsAndProvidedSha1Match");

	assetUsagePolicy.fileMissing = getBool("Content/AssetUsagePolicy/fileMissing");
	assetUsagePolicy.fileTooSmall = getBool("Content/AssetUsagePolicy/fileTooSmall");
	assetUsagePolicy.fileExistsAndNoSha1Provided = getBool("Content/AssetUsagePolicy/fileExistsAndNoSha1Provided");
	assetUsagePolicy.fileExistsAndProvidedSha1Missmatch = getBool("Content/AssetUsagePolicy/fileExistsAndProvidedSha1Missmatch");
	assetUsagePolicy.fileExistsAndProvidedSha1Match = getBool("Content/AssetUsagePolicy/fileExistsAndProvidedSha1Match");

	objectUsagePolicy.allObjectAssetsAreOK = getBool("Content/ObjectUsagePolicy/allAssetsAreOK");
	objectUsagePolicy.minNumberOfImageAssets = getBool("Content/ObjectUsagePolicy/minNumberImgAssets");
	objectUsagePolicy.minNumberOfVideoAssets = getBool("Content/ObjectUsagePolicy/minNumberVideoAssets");
	objectUsagePolicy.minNumberOfAudioAssets = getBool("Content/ObjectUsagePolicy/minNumberAudioAssets");

	renderSize.x = getInt("App/renderSize/width");
	renderSize.y = getInt("App/renderSize/height");
	timeSampleOfxApp = getBool("App/TimeSampleOfxApp");
}


void App::setupRuiWatches(){

	ofxJSON paramWatches = settings().getJson("RemoteUI/paramWatches");
	if(paramWatches.size()){
		for( auto itr = paramWatches.begin(); itr!=paramWatches.end() ; itr++){
			string paramName = itr.key().asString();
			bool shouldWatch = (*itr).asBool();
			if (shouldWatch){
				ofLogNotice("ofxApp") << "Adding RemoteUI Param Watch for '" << paramName << "'";
				RUI_WATCH_PARAM_WCN(paramName);
			}
		}
	}
}


void App::setupTimeMeasurements(){

	ofLogNotice("ofxApp") << "setupTimeMeasurements()";
	TIME_SAMPLE_SET_CONFIG_DIR(configsDir);
	TIME_SAMPLE_SET_FRAMERATE(getInt("App/frameRate", 60));
	bool enabled = getBool("TimeMeasurements/enabled", true);
	if(timeSampleOfxApp) enabled = true; //if we are benchmarking ofxApp, enable regardless
	TIME_SAMPLE_SET_ENABLED(enabled);
	TIME_SAMPLE_DISABLE_AVERAGE();
	TIME_SAMPLE_SET_DRAW_LOCATION((ofxTMDrawLocation)(getInt("TimeMeasurements/widgetLocation", 3)));
	TIME_SAMPLE_GET_INSTANCE()->setDeadThreadTimeDecay(getFloat("TimeMeasurements/threadTimeDecay"));
	TIME_SAMPLE_GET_INSTANCE()->setUiScale(getFloat("TimeMeasurements/uiScale", 1.0));
	bool useFontStash = getBool("TimeMeasurements/useFontStash");
	if(useFontStash){
		string fontFile = getString("TimeMeasurements/fontFile");
		ofxApp::utils::assertFileExists(fontFile);
		if( !ofIsGLProgrammableRenderer()){
			ofLogWarning("ofxApp") << "Using ofxFontStash for TimeMeasurements as we are using GL2!";
			TIME_SAMPLE_GET_INSTANCE()->drawUiWithFontStash(fontFile, getInt("TimeMeasurements/fontSize", 13));
		}else{
			ofLogWarning("ofxApp") << "Using ofxFontStash2 for TimeMeasurements as we are using GL3+!";
			TIME_SAMPLE_GET_INSTANCE()->drawUiWithFontStash2(fontFile, getInt("TimeMeasurements/fontSize", 13));
		}
	}
	TIME_SAMPLE_GET_INSTANCE()->setMsPrecision(getInt("TimeMeasurements/msPrecision", 2));
	TIME_SAMPLE_GET_INSTANCE()->setPlotResolution(getFloat("TimeMeasurements/plotResolution", 1.0));
	TIME_SAMPLE_SET_REMOVE_EXPIRED_THREADS(getBool("TimeMeasurements/removeExpiredThreads", true));
	TIME_SAMPLE_GET_INSTANCE()->setRemoveExpiredTimings(getBool("TimeMeasurements/removeExpiredTimings", false));
	TIME_SAMPLE_GET_INSTANCE()->setDrawPercentageAsGraph(getBool("TimeMeasurements/percentageAsGraph", true));
	TIME_SAMPLE_GET_INSTANCE()->setPlotHeight(getFloat("TimeMeasurements/plotH", 60));
}


void App::setupTuio(){
	ofLogNotice("ofxApp") << "setupTuio()";
	if(getBool("TUIO/enabled")){
		int port = getInt("TUIO/port");
		ofLogNotice("ofxApp") << "Listening for TUIO events at port " << port;
		tuioClient.start(port); //TODO - make sure we do it only once!
		ofAddListener(tuioClient.cursorAdded, delegate, &ofxAppDelegate::tuioAdded);
		ofAddListener(tuioClient.cursorRemoved, delegate, &ofxAppDelegate::tuioRemoved);
		ofAddListener(tuioClient.cursorUpdated, delegate, &ofxAppDelegate::tuioUpdated);
	}
}

#pragma mark - draw

void App::update(ofEventArgs &){

	tuioClient.getMessage();
	for(auto c : contentStorage){
		c.second->update(dt);
	}
	updateStateMachine(dt);
	if(gAnalytics) gAnalytics->update();
}


void App::exit(ofEventArgs &){

	ofLogWarning("ofxApp") << "OF is exitting!";
	if(gAnalytics) gAnalytics->sendEvent("ofxApp", "exitApp", 0, "", false);
	if (gAnalytics) delete gAnalytics;

	//if we are in the download stage, we would crash unless we stop the download threads
	//before deleting ofxApp::App so here we stop all downloads.
	ofLogWarning("ofxApp") << "Stopping any current downloads!";
	for (auto it : contentStorage) {
		it.second->stopAllDownloads();
	}

	ofLogWarning("ofxApp") << "Destroying ofxSimpleHttp SSL context...";
	ofxSimpleHttp::destroySslContext();
	ofLogWarning("ofxApp") << "Closing ThreadSafeLog(s)...";
	ofxThreadSafeLog::one()->close();

	ofLogWarning("ofxApp") << "Delete \"" << pidFileName <<"\" PID file";
	ofFile::removeFile(pidFileName);
	
	ofLogWarning("ofxApp") << "ofxApp ran for " << ofxApp::utils::secondsToHumanReadable(ofGetElapsedTimef(), 2);
	ofLogWarning("ofxApp") << "GoodBye!";
}

//////////////////// LOADING SCREEN /////////////////////////////////////////////////////////////////
#pragma Draw Loading Screen

void App::draw(ofEventArgs &){

	switch(appState.getState()){

		case State::SETUP_OFXAPP_INTERNALS:
		case State::SETUP_DELEGATE_B4_CONTENT_LOAD:
		case State::LOAD_STATIC_TEXTURES:
		case State::LOAD_JSON_CONTENT:
		case State::LOAD_JSON_CONTENT_FAILED:
		case State::DELIVER_CONTENT_LOAD_RESULTS:
		case State::SETUP_DELEGATE_B4_RUNNING:{
			ofSetupScreen();
			float w = ofGetWidth();
			float h = ofGetHeight();
			ofClear(0,0,0,255);
			appState.draw(ofRectangle(startupScreenViewport.x * w,
									  startupScreenViewport.y * h,
									  startupScreenViewport.width * w,
									  startupScreenViewport.height * h)
						  );
			}break;

		case State::RUNNING:
			drawStats(); break;

		case State::MAINTENANCE:
			drawMaintenanceScreen(); drawStats(); break;

		case State::DEVELOPER_REQUESTED_ERROR_SCREEN:
			drawErrorScreen(); drawStats(); break;
			break;

	}

	ofSetColor(0); mullions.draw(); ofSetColor(255);

}


void App::drawStats(){

	//stack up on screen stats
	int x = 20;
	int y = 27;
	int pad = -10;
	int fontSize = 15;

	if(globalsStorage->drawAppRunTime){
		ofRectangle r = drawMsgInBox("App Runtime: " + ofxApp::utils::secondsToHumanReadable(ofGetElapsedTimef(), 1), x, y, fontSize, ofColor::turquoise);
		y += r.height + fabs(r.y - y) + pad;
	}

	if(globalsStorage->drawStaticTexturesMemStats){
		float mb = one().textures().getTotalMemUsed();
		ofRectangle r = drawMsgInBox("ofxApp Static Textures Mem Used: " + ofToString(mb, 1) + "Mb", x, y, fontSize, ofColor::fuchsia);
		y += r.height + fabs(r.y - y) + pad;
	}

	if (globalsStorage->drawAutoTextureMemStats) {
		float mb = ofxAutoTexture::getTotalLoadedMBytes();
		ofRectangle r = drawMsgInBox("ofxAutoTexture Mem Used: " + ofToString(mb, 1) + "Mb", x, y, fontSize, ofColor::deepSkyBlue);
		y += r.height + fabs(r.y - y) + pad;
	}

	if(globalsStorage->drawTextureLoaderStats){
		ofRectangle r = drawMsgInBox(TexturedObjectStats::one().getStatsAsText(), x, y, fontSize, ofColor::orange);
		y += r.height + fabs(r.y - y) + pad;
	}

	if(globalsStorage->drawTextureLoaderState){
		ofRectangle r = drawMsgInBox(ProgressiveTextureLoadQueue::instance()->getStatsAsText(), x, y, fontSize, ofColor::limeGreen);
		y += r.height + fabs(r.y - y) + pad;
	}

	const string glErr = ofxApp::utils::getGlError();
	if(glErr.size()){
		ofRectangle r = drawMsgInBox("OpenGL Error: " + glErr, x, y, fontSize, ofColor::red);
		y += r.height + fabs(r.y - y) + pad;
	}
}

void App::drawMaintenanceScreen(){

	string settName = "MaintenanceMode";
	ofColor bgcolor = getColor("App/" + settName + "/bgColor");

	//read layout settings
	float x = getFloat("App/" + settName + "/layout/x");
	float y = getFloat("App/" + settName + "/layout/y");
	float colW = getFloat("App/" + settName + "/layout/width");
	float rotation = getFloat("App/" + settName + "/layout/rotation");
	float scale = getFloat("App/" + settName + "/layout/scale");

	//read header settings
	string header = getString("App/" + settName + "/header/text");
	float headerSpacing = getFloat("App/" + settName + "/header/spacing");
	float headerScaleup = getFloat("App/" + settName + "/header/fontScaleup");
	string headerFont = getString("App/" + settName + "/header/fontID");
	ofColor headerColor = getColor("App/" + settName + "/header/color");

	//read body settings
	string body = getString("App/" + settName + "/body/text");
	float bodySpacing = getFloat("App/" + settName + "/body/spacing");
	float bodyScaleup = getFloat("App/" + settName + "/body/fontScaleup");
	string bodyFont = getString("App/" + settName + "/body/fontID");
	ofColor bodyColor = getColor("App/" + settName + "/body/color");

	if(!G_FS2().isFontLoaded(headerFont)){
		if(ofGetFrameNum()%120 == 1) ofLogError("ofxApp") << "Maintenance Mode Font not found! " << headerFont;
		headerFont = "mono";
	}

	ofClear(bgcolor);

	float fontSize = scale * ofGetHeight() / 30.;
	float colWidth = ofGetWidth() * colW;
	float headerY = ofGetHeight() * y;
	float headerX = ofGetWidth() * x;

	ofPushMatrix();
	ofTranslate(headerX, headerY);
	ofRotateDeg(rotation, 0, 0, 1);
	ofxFontStash2::Style headerStyle = ofxFontStash2::Style(headerFont, fontSize * headerScaleup, headerColor);
	headerStyle.spacing = headerSpacing;
	int lineH = G_FS2().getTextBounds("Mp", headerStyle, 0, 0).height;
	ofRectangle headerRect = G_FS2().drawColumn(header, headerStyle, -colWidth/2, 0, colWidth , OF_ALIGN_HORZ_CENTER);

	ofxFontStash2::Style bodyStyle = ofxFontStash2::Style(bodyFont, fontSize * bodyScaleup, bodyColor);
	bodyStyle.spacing = bodySpacing;
	ofRectangle bodyRect = G_FS2().drawColumn(body, bodyStyle, -colWidth/2, headerRect.getBottom() + 1.5 * lineH, colWidth, OF_ALIGN_HORZ_CENTER);
	ofPopMatrix();
}


void App::drawErrorScreen(){

	string settName = "ErrorScreen";
	ofColor bgcolor = getColor("App/" + settName + "/bgColor");

	//read layout settings
	float x = getFloat("App/" + settName + "/layout/x");
	float y = getFloat("App/" + settName + "/layout/y");
	float colW = getFloat("App/" + settName + "/layout/width");
	float rotation = getFloat("App/" + settName + "/layout/rotation");
	float scale = getFloat("App/" + settName + "/layout/scale");

	//read header settings
	float headerSpacing = getFloat("App/" + settName + "/title/spacing");
	float headerScaleup = getFloat("App/" + settName + "/title/fontScaleup");
	string headerFont = getString("App/" + settName + "/title/fontID");
	ofColor headerColor = getColor("App/" + settName + "/title/color");

	//read body settings
	float bodySpacing = getFloat("App/" + settName + "/body/spacing");
	float bodyScaleup = getFloat("App/" + settName + "/body/fontScaleup");
	string bodyFont = getString("App/" + settName + "/body/fontID");
	ofColor bodyColor = getColor("App/" + settName + "/body/color");

	if(!G_FS2().isFontLoaded(headerFont)){
		if(ofGetFrameNum()%120 == 1) ofLogError("ofxApp") << "Maintenance Mode Font not found! " << headerFont;
		headerFont = "mono";
	}

	ofClear(bgcolor);

	float fontSize = scale * ofGetHeight() / 30.;
	float colWidth = ofGetWidth() * colW;
	float headerY = ofGetHeight() * y;
	float headerX = ofGetWidth() * x;

	ofPushMatrix();
	ofTranslate(headerX, headerY);
	ofRotateDeg(rotation, 0, 0, 1);
		ofxFontStash2::Style headerStyle = ofxFontStash2::Style(headerFont, fontSize * headerScaleup, headerColor);
		headerStyle.spacing = headerSpacing;
		int lineH = G_FS2().getTextBounds("Mp", headerStyle, 0, 0).height;
		ofRectangle headerRect = G_FS2().drawColumn(errorStateHeader, headerStyle, -colWidth/2, 0, colWidth, OF_ALIGN_HORZ_CENTER);

		ofxFontStash2::Style bodyStyle = ofxFontStash2::Style(bodyFont, fontSize * bodyScaleup, bodyColor);
		bodyStyle.spacing = bodySpacing;
		ofRectangle bodyRect = G_FS2().drawColumn(errorStateBody, bodyStyle, -colWidth/2, headerRect.getBottom() + 1.5 * lineH, colWidth, OF_ALIGN_HORZ_CENTER);
	ofPopMatrix();
}


void App::onDrawLoadingScreenStatus(ofRectangle & area){

	switch (appState.getState()) {

		case State::LOAD_STATIC_TEXTURES:{
			textures().drawAll(area);
			float progress = textures().getNumLoadedTextures() / float(textures().getNumTextures());
			string msg = ofToString(textures().getTotalMemUsed(), 1) + " Mb used";
			appState.updateState(progress, msg);
		}break;

		case State::SETUP_DELEGATE_B4_CONTENT_LOAD:
		case State::DELIVER_CONTENT_LOAD_RESULTS:
		case State::SETUP_DELEGATE_B4_RUNNING:
			delegate->ofxAppDrawPhaseProgress((Phase)appState.getState(), area);
			break;

		default: break;
	}
}


#pragma mark State Machine

void App::updateStateMachine(float dt){

	switch (appState.getState()) {

		case State::SETUP_DELEGATE_B4_CONTENT_LOAD:
			if (delegate->ofxAppIsPhaseComplete(Phase(State::SETUP_DELEGATE_B4_CONTENT_LOAD))) {
				ofLogNotice("ofxApp") << "Done SETUP_DELEGATE_B4_CONTENT_LOAD!";
				appState.setState(State::LOAD_STATIC_TEXTURES);
			}else{
				appState.updateState(delegate->ofxAppGetProgressForPhase(Phase(State::SETUP_DELEGATE_B4_CONTENT_LOAD)),
									 delegate->ofxAppGetLogString(Phase(State::SETUP_DELEGATE_B4_CONTENT_LOAD))
									 );
				appState.setProgressBarExtraInfo("- " + delegate->ofxAppGetStatusString(Phase(State::SETUP_DELEGATE_B4_CONTENT_LOAD)));
			}break;

		case State::LOAD_JSON_CONTENT:

			appState.updateState(contentStorage[currentContentID]->getPercentDone(),
								 contentStorage[currentContentID]->getStatus()
								 );

			if(appState.isReadyToProceed() ){ //slow down the state machine to handle error / retry

				if( appState.hasError() && appState.ranOutOfErrorRetries()){ //give up!
					ofLogError("ofxApp") << "json failed to load too many times! Giving Up!";
					appState.setState(State::LOAD_JSON_CONTENT_FAILED);
					
					OFXAPP_REPORT(	"ofxAppJsonContentGiveUp", "Giving up on fetching JSON for '" + currentContentID +
							 		"'!\nJsonSrc: \"" + contentStorage[currentContentID]->getJsonDownloadURL() +
								 	"\"\nStatus: " + contentStorage[currentContentID]->getStatus() +
								 	"\"ErrorMsg: \"" + contentStorage[currentContentID]->getErrorMsg(),
								 	2);
					break;
					
				}else{
					
					if(contentStorage[currentContentID]->isContentReady()){ //see if we are done
						logBanner("JSON content \"" + currentContentID + "\" loaded! " + ofToString(contentStorage[currentContentID]->getNumParsedObjects()) + " objects.");
						loadedContent.push_back(currentContentID);
						if(timeSampleOfxApp) TS_STOP_NIF("ofxApp LoadContent " + currentContentID);

						if(loadedContent.size() == contentStorage.size()){ //done loading ALL the JSON contents!
							appState.setState(State::DELIVER_CONTENT_LOAD_RESULTS);
						}else{ //load the next json
							currentContentID = requestedContent[loadedContent.size()];
							appState.setState(State::LOAD_JSON_CONTENT);
						}
						break;
					}
				}

				if(contentStorage[currentContentID]->foundError()){
					int numRetries = getInt("StateMachine/onErrorRetryCount", 5);
					int delaySeconds = getInt("StateMachine/onErrorWaitTimeSec", 5);
					appState.setError("failed to load content for \"" + currentContentID + "\"", delaySeconds /*sec*/, numRetries /*retry max*/); //report an error, retry!
					ofLogError("ofxApp") << "json failed to load! (" << appState.getNumTimesRetried() << ")";
					if(numRetries > 0){ //if no retry allowed, jump to fail state directly
						appState.setState(State::LOAD_JSON_CONTENT, false); //note "false" << do not clear errors (to keep track of # of retries)
					}else{
						appState.setState(State::LOAD_JSON_CONTENT_FAILED, false);  //note "false" << do not clear errors (to keep track of # of retries)
					}
				}
			}
			break;

		case State::LOAD_JSON_CONTENT_FAILED:{
			if(contentStorage[currentContentID]->isReadyToFetchContent()){
				string knownGoodJSON = "file://" + contentStorage[currentContentID]->getLastKnownGoodJsonPath();
				contentStorage[currentContentID]->setJsonDownloadURL(knownGoodJSON); //lets try from a known good json
				appState.setState(State::LOAD_JSON_CONTENT, false);
			}else{
				if(appState.getElapsedTimeInCurrentState() < 0.016){
					ofLogNotice("ofxApp") << "Json Content Load Failed but ofxAppContent not ready yet... Waiting.";
				}
			}
			}break;

		case State::DELIVER_CONTENT_LOAD_RESULTS:
			if(delegate->ofxAppIsPhaseComplete(Phase(State::DELIVER_CONTENT_LOAD_RESULTS))){
				ofLogNotice("ofxApp") << "Done DELIVER_CONTENT_LOAD_RESULTS!";
				appState.setState(State::SETUP_DELEGATE_B4_RUNNING);
			}else{
				appState.updateState(delegate->ofxAppGetProgressForPhase(Phase(State::DELIVER_CONTENT_LOAD_RESULTS)),
									 delegate->ofxAppGetLogString(Phase(State::DELIVER_CONTENT_LOAD_RESULTS))
									 );
				appState.setProgressBarExtraInfo("- " + delegate->ofxAppGetStatusString(Phase(State::DELIVER_CONTENT_LOAD_RESULTS)));
			}break;

		case State::SETUP_DELEGATE_B4_RUNNING:
			if (delegate->ofxAppIsPhaseComplete(Phase(State::SETUP_DELEGATE_B4_RUNNING))) {
				ofLogNotice("ofxApp") << "Done SETUP_DELEGATE_B4_RUNNING!";
				appState.setState(State::RUNNING);
			}else{
				appState.updateState(delegate->ofxAppGetProgressForPhase(Phase(State::SETUP_DELEGATE_B4_RUNNING)),
									 delegate->ofxAppGetLogString(Phase(State::SETUP_DELEGATE_B4_RUNNING))
									 );
				appState.setProgressBarExtraInfo("- " + delegate->ofxAppGetStatusString(Phase(State::SETUP_DELEGATE_B4_RUNNING)));
			}break;

		case State::RUNNING:
			appState.updateState( -1, "");
			break;

		default: break;
	}
}


void App::onStateChanged(ofxStateMachine<State>::StateChangedEventArgs& change){

	ofLogNotice("ofxApp") 	<< "State Changed from \"" << appState.getNameForState(change.oldState)
							<< "\" to \"" << appState.getNameForState(change.newState) << "\". Previous State Duration: " << change.timeInPrevState << "sec.";

	appState.setProgressBarExtraInfo("");

	switch(change.newState){

		case State::SETUP_DELEGATE_B4_CONTENT_LOAD:
			ofLogNotice("ofxApp") << "Start SETUP_DELEGATE_B4_CONTENT_LOAD...";
			delegate->ofxAppPhaseWillBegin(Phase(State::SETUP_DELEGATE_B4_CONTENT_LOAD));
		break;

		case State::LOAD_STATIC_TEXTURES:
			startLoadingStaticAssets();
			break;

		case State::LOAD_JSON_CONTENT:{
			if(change.oldState != State::LOAD_JSON_CONTENT_FAILED){
				if(timeSampleOfxApp) TS_START_NIF("ofxApp LoadContent " + currentContentID);
				logBanner("Start Loading Content  \"" + currentContentID + "\"");
				
				bool keyExists = settings().exists("Content/JsonSources/" + currentContentID);
				
				if(keyExists){
					string jsonURL = getString("Content/JsonSources/" + currentContentID + "/url");
					string jsonDir = getString("Content/JsonSources/" + currentContentID + "/jsonDownloadDir");
					bool skipPolicyTests = getBool("Content/JsonSources/" + currentContentID + "/shouldSkipObjectPolicyTests");
					
					int numConcurrentDownloads = getInt("Downloads/maxConcurrentDownloads");
					int numThreads = getInt("App/maxThreads");
					int timeOutSecs = getInt("Downloads/timeOutSec");
					int speedLimitKBs = getInt("Downloads/speedLimitKb");
					float idleTimeAfterDl = getFloat("Downloads/idleTimeAfterEachDownloadSec");
					string assetDownloadLocation = getString("Content/JsonSources/" + currentContentID + "/assetsLocation");

					contentStorage[currentContentID]->setup(currentContentID,
															jsonURL,
															jsonDir,
															numThreads,
															numConcurrentDownloads,
															speedLimitKBs,
															timeOutSecs,
															skipPolicyTests,
															idleTimeAfterDl,
															credentials,
															proxyCfg,
															contentCfgs[currentContentID],
															assetDownloadPolicy,
															assetUsagePolicy,
															objectUsagePolicy,
															assetDownloadLocation
													  );
					
					contentStorage[currentContentID]->fetchContent(); //this starts the ofxAppContent process!
					
				}else{
					ofxApp::utils::terminateApp("ofxApp", "Requested content ID \"Content/JsonSources/" + currentContentID + "\" not found in \"" + settingsFile + "\"");
				}

			}else{ //We are retrying to download with a known good json! we already swapped the JSON URL to a local older JSON
				ofLogNotice("ofxApp") << "fetching content from a known good JSON!";
				contentStorage[currentContentID]->fetchContent(); //this starts the ofxAppContent process!
			}
			}break;

		case State::LOAD_JSON_CONTENT_FAILED:
			appState.setProgressBarExtraInfo("- CONTENT LOAD FAILED");
			//ofxSuperLog::getLogger()->setScreenLoggingEnabled(true); //show log if json error
			break;

		case State::DELIVER_CONTENT_LOAD_RESULTS:
			for(auto c : contentStorage){
				delegate->ofxAppContentIsReady(c.first, c.second->getParsedObjects());
			}
			ofLogNotice("ofxApp") << "Start Loading Custom User Content...";
			delegate->ofxAppPhaseWillBegin(Phase(State::DELIVER_CONTENT_LOAD_RESULTS));
			break;

		case State::SETUP_DELEGATE_B4_RUNNING:
			setupRuiWatches();
			setupApp();
			ofLogNotice("ofxApp") << "Start SETUP_DELEGATE_B4_RUNNING...";
			if(gAnalytics) gAnalytics->sendEvent("ofxApp", "startApp", 0, "", false);
			delegate->ofxAppPhaseWillBegin(Phase(State::SETUP_DELEGATE_B4_RUNNING)); //user custom code runs here
			break;

		case State::RUNNING:{
			float ts = -1.0f;
			if(change.oldState != State::DEVELOPER_REQUESTED_ERROR_SCREEN){
				if(timeSampleOfxApp){
					ts = TS_STOP_NIF("ofxApp Setup");
				}
				logBanner(" ofxApp Setup Complete! " + ofToString(ofGetElapsedTimef(), 2) + "sec." );
			}
			}
			break;

		default: break;
	}
}


void App::onStateError(ofxStateMachine<State>::ErrorStateEventArgs& error){
	ofLogError("ofxApp") << "Error '" << error.errorMsg << "' during state '" << appState.getNameForState(error.state) << "'";
}


void App::onContentManagerStateChanged(string& s){
	appState.setProgressBarExtraInfo(": " + s); // add our sub-state name to the loading screen
}


bool App::enterErrorState(string errorHeader, string errorBody){

	if(appState.getState() == State::RUNNING || appState.getState() == State::DEVELOPER_REQUESTED_ERROR_SCREEN){
		errorStateHeader = errorHeader;
		errorStateBody = errorBody;
		appState.setState(State::DEVELOPER_REQUESTED_ERROR_SCREEN);
		ofLogWarning("ofxApp") << "enterErrorState() - \"" << errorHeader << "\" : \"" << errorBody << "\"";
		return true;
	}
	ofLogError("ofxApp") << "can't enterErrorState() until we hit the RUNNING State";
	return false;
}


bool App::exitErrorState(){
	if(appState.getState() == State::DEVELOPER_REQUESTED_ERROR_SCREEN){
		errorStateHeader = "";
		errorStateBody = "";
		appState.setState(State::RUNNING);
		ofLogWarning("ofxApp") << "exitErrorState()";
		return true;
	}
	ofLogError("ofxApp") << "cant exitErrorState() unless we are in DEVELOPER_REQUESTED_ERROR_SCREEN State";
	return false;
}


void App::onStaticTexturesLoaded(){
	ofLogNotice("ofxApp")<< "All Static Textures Loaded!";
	if(timeSampleOfxApp) TS_STOP_NIF("ofxApp Load Static Textures");
	if(contentStorage.size()){
		appState.setState(State::LOAD_JSON_CONTENT);
	}else{
		ofLogWarning("ofxApp")<< "Skipping JsonLoadContent phase, as there's no content to load.";
		appState.setState(State::DELIVER_CONTENT_LOAD_RESULTS);
	}
}

#pragma mark Callbacks

void App::onRemoteUINotification(RemoteUIServerCallBackArg &arg){
	switch (arg.action) {
		case CLIENT_UPDATED_PARAM:
			if(arg.paramName == "showMouse"){
				if(arg.param.boolVal) ofShowCursor();
				else ofHideCursor();
			}
			if(arg.paramName == "enableMouse"){
				setMouseEvents(arg.param.boolVal);
			}
			if(arg.paramName == "bgColor"){
				ofBackground(colorsStorage.bgColor);
				RUI_PUSH_TO_CLIENT();
			}
			break;
		default:
			break;
	}
}


void App::onKeyPressed(ofKeyEventArgs & a){
	bool didPress = false;
	switch(a.key){
		case 'W': screenSetup.cycleToNextScreenMode(); break;
		case 'L': {
			if(getBool("Logging/toScreen")){
				ofxSuperLog::getLogger()->setScreenLoggingEnabled(!ofxSuperLog::getLogger()->isScreenLoggingEnabled());
				break;
			}
		}
		case 'R': loadSettings(); RUI_LOG("[ofxApp : keyPress 'R'] Loaded Settings from \"ofxAppSettings.json\""); break;
		case 'M': mullions.toggle(); RUI_LOG("[ofxApp : keyPress 'M'] Toggled Mullions"); break;
		case 'D': globalsStorage->debug ^= true; didPress = true; break;
	}
	if(didPress){
		RUI_PUSH_TO_CLIENT();
	}
}

void App::screenSetupChanged(ofxScreenSetup::ScreenSetupArg &arg){
	if(delegate) delegate->screenSetupChanged(arg);
}

#pragma mark Utils

ofRectangle App::getRenderAreaForCurrentWindowSize(){
	ofRectangle win = ofRectangle(0, 0, ofGetWindowWidth(), ofGetWindowHeight());
	ofRectangle render = ofRectangle(0, 0, one().renderSize.x, one().renderSize.y);
	render.scaleTo(win);
	return render;
}


ofRectangle App::getRenderRect() {
	return ofRectangle(0, 0, one().renderSize.x, one().renderSize.y);
}


void App::logBanner(const string & log){
	ofLogNotice("ofxApp") << "";
	#ifdef TARGET_WIN32
	ofLogNotice("ofxApp") << "///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////";
	ofLogNotice("ofxApp") << "// " << log;
	ofLogNotice("ofxApp") << "///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////";
	#else
	ofLogNotice("ofxApp") << "███████████████████████████████████████████████████████████████████████████████████████████████████████████████████████";
	ofLogNotice("ofxApp") << "██ " << log;
	ofLogNotice("ofxApp") << "███████████████████████████████████████████████████████████████████████████████████████████████████████████████████████";
	#endif
	ofLogNotice("ofxApp") << "";
}

ofRectangle App::drawMsgInBox(string msg, int x, int y, int fontSize, ofColor fontColor, ofColor bgColor, float edgeGrow) {

	ofRectangle bbox;
	if(!ofIsGLProgrammableRenderer()){ //use ofxFontStash
		if (msg.size() == 0) return ofRectangle();
		ofxFontStash & font = fonts().getMonoBoldFont();
		bbox = font.getBBox(msg, fontSize, x, y);
		ofSetColor(bgColor);
		bbox.x -= edgeGrow; bbox.y -= edgeGrow; bbox.width += 2 * edgeGrow; bbox.height += 2 * edgeGrow;
		ofDrawRectangle(bbox);
		ofSetColor(fontColor);
		font.drawMultiLine(msg, fontSize, x, y);
	}else{ //use ofxFontStash2
		ofxFontStash2::Style style = ofxFontStash2::Style(fonts().monoBoldID, fontSize, fontColor);
		auto bbox = fonts().getFontStash2().getTextBoundsNVG(msg, style, x, y, ofGetWidth(), OF_ALIGN_HORZ_LEFT );
		ofSetColor(bgColor);
		bbox.x -= edgeGrow; bbox.y -= edgeGrow; bbox.width += 2 * edgeGrow; bbox.height += 2 * edgeGrow;
		ofDrawRectangle(bbox);
		fonts().getFontStash2().drawColumnNVG(msg, style, x, y, bbox.width, OF_ALIGN_HORZ_LEFT);
		//ofDrawBitmapStringHighlight(msg, x, y, bgColor, fontColor);
		//auto lines = ofSplitString(msg, "\n");
		//float lineH = 14;
		//float boxH = lineH * MAX(lines.size(),1) + 18;
		//bbox = ofRectangle(x, y, msg.size() * 8, boxH );
	}
	ofSetColor(255);
	return bbox;
}


bool App::isJsonContentDifferentFromLastLaunch(string contentID, string & freshJsonSha1, string & oldJsonSha1){

	if(contentStorage.find(contentID) == contentStorage.end()){
		ofLogError("ofxApp") << "cant find content matching this contentID \"" << contentID << "\"";
		return false;
	}
	freshJsonSha1 = contentStorage[contentID]->getFreshJsonSha1();
	oldJsonSha1 = contentStorage[contentID]->getOldJsonSha1();

	ofLogNotice("ofxApp") << "JSON sha1s for contentID: \"" << contentID << "\" freshJson: \"" << freshJsonSha1 << "\" oldJson: \"" << oldJsonSha1 << "\"";
	return freshJsonSha1 != oldJsonSha1;
}

///////////////////// SETTINGS //////////////////////////////////////////////////////////////////////
#pragma mark Settings

bool& App::getBool(const string & key, bool defaultVal){
	if(!hasLoadedSettings) ofLogError("ofxApp") << "Trying to get a BOOL setting but Settings have not been loaded! '" << key<< "'";
	if(settings().exists(key) && hasLoadedSettings){
		if(VERBOSE_SETTINGS_ACCESS) ofLogNotice("ofxApp") << FILE_ACCES_ICON << " Getting Bool Value for \"" << key << "\" : " << settings().getBool(key);
		return settings().getBool(key);
	}else{
		string msg = "Requesting a BOOL setting that does not exist! \"" + key + "\" in '" + settingsFile + "'";
		ofLogFatalError("ofxApp") << msg;
		if(QUIT_ON_MISSING_SETTING) ofxApp::utils::terminateApp("ofxApp", msg);
		static auto def = defaultVal;
		return def; //mmmm....
	}
}


int& App::getInt(const string & key, int defaultVal){
	if(!hasLoadedSettings) ofLogError("ofxApp") << "Trying to get a INT setting but Settings have not been loaded! '" << key<< "'";
	if(settings().exists(key) && hasLoadedSettings){
		if(VERBOSE_SETTINGS_ACCESS) ofLogNotice("ofxApp") << FILE_ACCES_ICON << " Getting Int Value for \"" << key << "\" : " << settings().getInt(key);
		return settings().getInt(key);
	}else{
		string msg = "Requesting an INT setting that does not exist! \"" + key + "\" in '" + settingsFile + "'";
		ofLogFatalError("ofxApp") << msg;
		if(QUIT_ON_MISSING_SETTING) ofxApp::utils::terminateApp("ofxApp", msg);
		static auto def = defaultVal;
		return def; //mmmm....
	}
}

float& App::getFloat(const string & key, float defaultVal){
	if(!hasLoadedSettings) ofLogError("ofxApp") << "Trying to get a FLOAT setting but Settings have not been loaded! '" << key<< "'";
	if(settings().exists(key) && hasLoadedSettings){
		if(VERBOSE_SETTINGS_ACCESS) ofLogNotice("ofxApp") << FILE_ACCES_ICON << " Getting Float Value for \"" << key << "\" : " << settings().getFloat(key);
		return settings().getFloat(key);
	}else{
		string msg = "Requesting a FLOAT setting that does not exist! \"" + key + "\" in '" + settingsFile + "'";
		ofLogFatalError("ofxApp") << msg;
		if(QUIT_ON_MISSING_SETTING) ofxApp::utils::terminateApp("ofxApp", msg);
		static auto def = defaultVal;
		return def; //mmmm....
	}
}

string& App::getString(const string & key, const string & defaultVal){
	if(!hasLoadedSettings) ofLogError("ofxApp") << "Trying to get a STRING setting but Settings have not been loaded! '" << key<< "'";
	if(settings().exists(key) && hasLoadedSettings){
		if(VERBOSE_SETTINGS_ACCESS) ofLogNotice("ofxApp") << FILE_ACCES_ICON << " Getting String Value for \"" << key << "\" : " << settings().getString(key);
		return settings().getString(key);
	}else{
		string msg = "Requesting a STRING setting that does not exist! \"" + key + "\" in '" + settingsFile + "'";
		ofLogFatalError("ofxApp") << msg;
		if(QUIT_ON_MISSING_SETTING) ofxApp::utils::terminateApp("ofxApp", msg);
		static auto def = defaultVal;
		return def; //mmmm....
	}
}

ofColor& App::getColor(const string & key, ofColor defaultVal){
	if(!hasLoadedSettings) ofLogError("ofxApp") << "Trying to get a COLOR setting but Settings have not been loaded! '" << key<< "'";
	if(settings().exists(key) && hasLoadedSettings){
		if(VERBOSE_SETTINGS_ACCESS) ofLogNotice("ofxApp") << FILE_ACCES_ICON << " Getting Color Value for \"" << key << "\" : " << settings().getColor(key);
		return settings().getColor(key);
	}else{
		string msg = "Requesting a COLOR setting that does not exist! \"" + key + "\" in '" + settingsFile + "'";
		ofLogFatalError("ofxApp") << msg;
		if(QUIT_ON_MISSING_SETTING) ofxApp::utils::terminateApp("ofxApp", msg);
		static auto def = defaultVal;
		return def; //mmmm....
	}
}

bool App::settingExists(const string & key){
	if(!hasLoadedSettings){
		ofLogError("ofxApp") << "Trying to get a COLOR setting but Settings have not been loaded! '" << key<< "'";
		return false;
	}
	return settings().exists(key);
}

//////////////////////////////////////////////////////////////////////////
