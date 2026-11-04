
import React, {useEffect, useRef, useState} from 'react';
import { FaChartBar, FaCloud, FaCog, FaMousePointer, FaDatabase, FaEdit, FaExchangeAlt, FaLocationArrow, FaRssSquare } from 'react-icons/fa';

import CarouselNav from './CarouselNav.js';
import ButtonGroup from './ButtonGroup.js';
import CarouselImage from './CarouselImage.js'

/* 存放图片地址.  The constructor built this list once and nothing ever wrote to
   it, so it is the same list for every carousel there will ever be. */
const imageSrc = [
  '../../../lib/images/banner-bg.jpg',
  '../../../lib/images/banner-bg.jpg',
  '../../../lib/images/banner-bg.jpg'];

export default function Carousel() {
  /* 当前展示的图片索引 */
  const [currentIndex, setCurrentIndex] = useState(0);
  /* 定时器引用 */
  const timer = useRef(null);

  /**
   * 工具函数，用于更新state，以及刷新定时器
   * @param  {number} index 将要展示图片的索引
   * @param  {number} len   展示图片总张数
   * @param  {number} delay 动画持续时间
   *
   * The tick read this.state.currentIndex at the moment it fired rather than the
   * one the timer was built with, which is what the updater form of a setter is.
   */
  const _updateIndex = (index, len, delay=4000) => {
    /* 清除定时器 */
    timer.current && clearInterval(timer.current);
    /* 设置当前展示图片 */
    setCurrentIndex(index);
    /* 打开定时器 */
    timer.current = setInterval(
            () => {
                setCurrentIndex(currentIndex => (currentIndex + 1) % len);
            },
            delay
        );
  };

  /**
   * 组件加载完毕后，图片自动播放
   * 组件卸载时，清理定时器
   */
  useEffect(() => {
    timer.current = setInterval(
            () => {
                setCurrentIndex(currentIndex => (currentIndex + 1) % 5);
            },
            500000
        );
    return () => {
      timer.current && clearInterval(timer.current);
    };
  }, []);

  /**
   * 展示前一张图片
   */
  const prevImage = () => {
    let index = currentIndex;
    const len = imageSrc.length;
    /* 计算下一张图片索引 */
    index = (index - 1) >= 0 ?
        (index - 1) % len : len - 1;
    /* 调用更新函数，更新当前显示的图片，并刷新定时器 */
    _updateIndex(index, len);
  };

  /**
   * 展示下一张图片
   */
  const nextImage = () => {
    let index = currentIndex;
    const len = imageSrc.length;
    /* 计算下一张图片索引 */
    index = (index + 1) % len;
    /* 调用更新函数，更新当前显示的图片，并刷新定时器 */
    _updateIndex(index, len);
  };

  /**
   * 展示选中索引图片
   * @param  {number} index 索引值
   */
  const selectImage = (index) => {
    const len = imageSrc.length;
    _updateIndex(index, len);
  };

  return (
    <div className="carousel">
      <CarouselImage
        imageSrc={imageSrc}
        currentIndex={currentIndex}
        component={"li"}
        name={"carousel-image-item"}
      />
      {/*<CarouselNav
        carouselNavItems={imageSrc}
        currentIndex={currentIndex}
        selectImage={selectImage}
      />*/}
      <ButtonGroup
        prevImage={prevImage}
        nextImage={nextImage}
      />
      <div className="introduction">
        <h2>产品介绍</h2>
        <p>本系统主要通过WIFI探针收集MAC地址、出现时间、出现地点、与探针距离，探针设备会定时发送数据到服务端， 通过一段时间数据平台产生大量的用户数据，然后使用大数据分析技术， 采用离线计算和实时计算技术相结合的方式，能够对商业环境中门店的门前人流量、进店客流量、进入跳出量、新老顾客数量及新增量、在店平均时长等数据项进行分析 为销售策略调整提供参考。</p>
        {/*<ul className='introductionWall'>
          <li><FaMousePointer /><p>操作简单</p><span>各项数据信息明确，操作简单</span></li>
          <li><FaDatabase /><p>内容详细</p><span>各项数据图文+表格展示，内容详细</span></li>
          <li><FaEdit /><p>自定义阈值</p><span>如新老顾客、深访率等支持自定义设置</span></li>
          <li><FaExchangeAlt /><p>数据对比</p><span>支持不同商家各数据项不同时间段对比</span></li>
          <li><FaEdit /><p>自定义时间</p><span>可以选择任意时间或时间段查询</span></li>
          <li><FaLocationArrow /><p>一键管理设备</p><span>远程设备管理方便快捷</span></li>
        </ul>*/}

        <ul className='introductionTable'>
          <li><FaChartBar /><p>数据统计</p><br /><span>系统包含商城与店铺各11项指标展示。</span></li>
          <li><FaCloud /><p>多样化数据</p><br /><span>系统支持从日周月等多时间维度对数据进行“二次编排”。</span></li>
          <li><FaCog /><p>信息管理</p><br /><span>系统支持对店铺,统计阈值等信息进行管理。</span></li>
          <li><FaRssSquare /><p>设备远程控制</p><br /><span>系统支持对探针设备远程进行重启，关机，上传控件等操作。</span></li>
        </ul>
      </div>
      <div className='introductionFooter'>
        <p>Oldpie</p>
        <p>© 2017 广东海洋大学 </p>
      </div>
    </div>
  );
}
