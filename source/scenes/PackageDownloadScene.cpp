// Stellar Updater
// Copyright (C) 2020 Nichole Mattera
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <cstring>
#include <jansson.h>
#include <SimpleIniParser.hpp>

#include "PackageDownloadScene.hpp"

#include "../ConfigManager.hpp"
#include "../FileManager.hpp"
#include "../SceneDirector.hpp"

#define IRAM_PAYLOAD_MAX_SIZE 0x2F000
#define IRAM_PAYLOAD_BASE 0x40010000

static __attribute__((aligned(0x1000))) u8 g_ff_page[0x1000];
static __attribute__((aligned(0x1000))) u8 g_work_page[0x1000];

using namespace dsu;
using namespace dsu::models;
using namespace dsu::views;
using namespace simpleIniParser;
using namespace std;
using namespace std::placeholders;
using namespace swurl;

namespace dsu::scenes {
    PackageDownloadScene::PackageDownloadScene() {
        SessionManager::onProgressChanged = bind(&PackageDownloadScene::_onProgressUpdate, this, _1, _2);
        SessionManager::onCompleted = bind(&PackageDownloadScene::_onCompleted, this, _1);
        SessionManager::onError = bind(&PackageDownloadScene::_onError, this, _1, _2);

        bpcInitialize();
        
        _headerView = new HeaderView("Stellar Updater", true);
        _headerView->frame = { 0, 0, 1280, 88 };

        _updateView = new UpdateView("Downloading the latest Stellar...");
        _updateView->frame.x = 0;
        _updateView->frame.y = 200;

        _statusView = new StatusView("", "");
        _statusView->frame.x = 0;
        _statusView->frame.y = 323;
        _statusView->hidden = true;

        _footerView = new FooterView();
        _footerView->frame = { 0, 647, 1280, 73 };

        addSubView(_headerView);
        addSubView(_updateView);
        addSubView(_statusView);
        addSubView(_footerView);
    }

    PackageDownloadScene::~PackageDownloadScene() {
        if (_headerView != NULL)
            delete _headerView;

        if (_updateView != NULL)
            delete _updateView;

        if (_statusView != NULL)
            delete _statusView;

        if (_footerView != NULL)
            delete _footerView;

        if (_StellarUrlRequest != NULL)
            delete _StellarUrlRequest;

        if (_StellarRequest != NULL)
            delete _StellarRequest;

        bpcExit();
    }

    void PackageDownloadScene::handleButton(u32 buttons, double dTime) {
        if (!_statusView->hidden && buttons & KEY_A) {
            if (_updateSuccessful) {
                auto payload = FileManager::readFile("romfs:/stellar_rcm.bin");
                if (payload.size() == 0) {
                    SceneDirector::exitApp = true;
                    return;
                }

                Result rc = splInitialize();
                if (R_FAILED(rc)) {
                    SceneDirector::exitApp = true;
                    return;
                }

                this->_clearIram();

                for (size_t i = 0; i < IRAM_PAYLOAD_MAX_SIZE; i += 0x1000) {
                    this->_copyToIram(IRAM_PAYLOAD_BASE + i, &payload[i], 0x1000);
                }

                splSetConfig((SplConfigItem) 65001, 2);
                splExit();
            } else {
                SceneDirector::exitApp = true;
            }
        }
    }

    void PackageDownloadScene::render(SDL_Rect rect, double dTime) {
        if (_StellarUrlRequest == NULL) {
            _StellarUrlRequest = new WebRequest("https://api.github.com/repos/SpaceNX/Stellar/releases");
            SessionManager::makeRequest(_StellarUrlRequest);
        }

        Scene::render(rect, dTime);
    }

    void PackageDownloadScene::_copyToIram(uintptr_t iram_addr, void *buf, size_t size) {
        memcpy(g_work_page, buf, size);
        
        SecmonArgs args = {0};
        args.X[0] = 0xF0000201;             /* smcAmsIramCopy */
        args.X[1] = (uintptr_t)g_work_page; /* DRAM Address */
        args.X[2] = iram_addr;              /* IRAM Address */
        args.X[3] = size;                   /* Copy size */
        args.X[4] = 1;
        svcCallSecureMonitor(&args);
        
        memcpy(buf, g_work_page, size);
    }

    void PackageDownloadScene::_clearIram() {
        memset(g_ff_page, 0xFF, sizeof(g_ff_page));
        for(size_t i = 0; i < IRAM_PAYLOAD_MAX_SIZE; i += sizeof(g_ff_page)) {
            this->_copyToIram(IRAM_PAYLOAD_BASE + i, g_ff_page, sizeof(g_ff_page));
        }
    }

    void PackageDownloadScene::_showStatus(string text, string subtext, bool wasSuccessful) {
        _statusView->setText(text);
        _statusView->setSubtext(subtext);

        _updateView->hidden = true;
        _statusView->hidden = false;

        _updateSuccessful = wasSuccessful;

        if (wasSuccessful) {
            _footerView->actions.push_back(new Action(A_BUTTON, "Restart"));
        } else {
            _footerView->actions.push_back(new Action(A_BUTTON, "Quit"));
        }
    }

    void PackageDownloadScene::_onProgressUpdate(WebRequest * request, double progress) {
        _updateView->setProgress(progress);
        SceneDirector::currentSceneDirector->render();
    }

    void PackageDownloadScene::_onCompleted(WebRequest * request) {
        if (request == _StellarUrlRequest) {
            json_t * root = json_loads(request->response.rawResponseBody.c_str(), 0, NULL);
            if (!root || !json_is_array(root) || json_array_size(root) < 1) {
                if (root) {
                    json_decref(root);
                }

                _showStatus("Unable to parse response from GitHub API.", "Please restart the app to try again.[7]", false);
                return;
            }

            json_t * release = json_array_get(root, 0);
            if (!release || !json_is_object(release)) {
                json_decref(root);
                _showStatus("Unable to parse response from GitHub API.", "Please restart the app to try again.[8]", false);
                return;
            }

            json_t * tagName = json_object_get(release, "tag_name");
            if (!tagName || !json_is_string(tagName)) {
                json_decref(root);
                _showStatus("Unable to parse response from GitHub API.", "Please restart the app to try again.[9]", false);
                return;
            }

            _StellarVersion = json_string_value(tagName);

            json_t * assets = json_object_get(release, "assets");
            if (!assets || !json_is_array(assets) || json_array_size(assets) < 1) {
                json_decref(root);
                _showStatus("Unable to parse response from GitHub API.", "Please restart the app to try again.[10]", false);
                return;
            }

            std::string downloadUrl = "";
            for(size_t i = 0; i < json_array_size(assets); i++) {
                json_t * asset = json_array_get(assets, i);
                if (!asset || !json_is_object(asset)) {
                    continue;
                }

                json_t * name = json_object_get(asset, "name");
                if (!name || !json_is_string(name)) {
                    continue;
                }

                std::string assetName(json_string_value(name));
                if (assetName.compare(0, 8, "stellar_") != 0 || assetName.compare(assetName.length() - 4, 4, ".zip") != 0) {
                    continue;
                }

                json_t * browserDownloadUrl = json_object_get(asset, "browser_download_url");
                if (!browserDownloadUrl || !json_is_string(browserDownloadUrl)) {
                    continue;
                }

                downloadUrl = std::string(json_string_value(browserDownloadUrl));
                break;
            }

            json_decref(root);

            if (downloadUrl.length() == 0) {
                _showStatus("Unable to find the latest release assets.", "Please restart the app to try again.", false);
                return;
            }

            _StellarRequest = new WebRequest(downloadUrl);
            SessionManager::makeRequest(_StellarRequest);
        } else {
            FileManager::writeFile("temp.zip", request->response.rawResponseBody);

            _updateView->setText("Removing old package...");
            _updateView->setProgressBarHidden(true);
            SceneDirector::currentSceneDirector->render();

            FileManager::cleanUpFiles();

            _updateView->setText("Extracting the latest package...");
            SceneDirector::currentSceneDirector->render();

            if (!FileManager::extract("temp.zip", "sdmc:/")) {
                FileManager::deleteFile("temp.zip");
                _showStatus("There was an error while trying to extract files.", "Please restart the app to try again.", false);
                return;
            }

            FileManager::deleteFile("temp.zip");
            ConfigManager::setCurrentVersion(_StellarVersion);

            _updateView->setText("Applying disabled game cart option...");
            SceneDirector::currentSceneDirector->render();

            FileManager::applyNoGC();

            _showStatus("Stellar has been updated to version " + _StellarVersion + "!", "Please restart your Switch to finish the update.", true);
        }
    }

    void PackageDownloadScene::_onError(WebRequest * request, string error) {
        _showStatus(error, "Please restart the app to try again.", false);
    }
}
