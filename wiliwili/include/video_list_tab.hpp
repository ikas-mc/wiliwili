//
// Created by fang on 2022/4/22.
//

#pragma once

#include <borealis.hpp>
#include "activity/player_activity.hpp"
#include "presenter/home.hpp"
#include "view/video_grid.hpp"
#include "view/video_card.hpp"

using namespace brls;

class VideoListTab : public brls::Box, public Home
{
public:
    VideoListTab(){
        this->inflateFromXMLRes("xml/tabs/video_list.xml");
#ifndef __SWITCH__
        //todo: 貌似switch上启动时立刻访问网络会有问题
        this->requestData();
#endif
        this->registerAction("刷新", ControllerButton::BUTTON_X, [this](brls::View* view)-> bool {
            Logger::debug("==> requestRecommendVideoList");
            this->requestRecommendVideoList();
            return true;
        });
    }

    void onRecommendVideoList(const bilibili::RecommendVideoListResult &result) override{
        this->videoGrid->clearViews();
        Logger::debug("onRecommendVideoList 1");
        Application::blockInputs();
        for(auto& i : result){
            Logger::debug("bvid: {}", i.bvid);
                VideoCardView* v = new VideoCardView();
                v->setFocusable(true);
                //todo: 找到适合的图片尺寸
                //todo: 支持webp
                v->setCard(i.pic+"@672w_378h_1c.jpg", i.title, i.owner.name, i.pubdate,
                           i.stat.view, i.stat.danmaku, i.duration
                           );
                v->registerClickAction([i](brls::View* view)-> bool {
                    Application::pushActivity(new VideoDetailActivity(i.bvid));
                    return true;
                });
                videoGrid->addView(v, videoGrid->getChildren().size());
        }
        Application::giveFocus(this->videoGrid);
        Application::unblockInputs();
        this->videoGridScrollingFrame->setContentOffsetY(0, true);
        Logger::debug("onRecommendVideoList 2");
    }

    ~VideoListTab(){
        Logger::error("del VideoListTab");
    }


    static brls::View* create(){
        return new VideoListTab();
    }

private:
    BRLS_BIND(VideoGrid, videoGrid, "home/video_grid");
    BRLS_BIND(brls::ScrollingFrame, videoGridScrollingFrame, "home/video_scroll");

};