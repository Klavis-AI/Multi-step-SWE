import React from 'react';

/* 定义参数类型 */
/* 轮播图片组件.
 *
 * CSSTransitionGroup rendered the caller's `component` with the caller's
 * className around the current slide and, while an image was coming or going,
 * added <name>-enter / -leave classes to the children for the length of the
 * timeout it was given; once that timeout had passed the markup is that element
 * and its children, with no classes left over.  Rendering the element directly
 * is the same markup, and the animation itself belongs to the stylesheet.
 *
 * It also spread the two timeout props it did not recognise - enterDelay and
 * leaveDelay - straight onto that element.  React 15 dropped attributes it did
 * not know; later React puts them in the markup, so those two are gone from the
 * call as well as from here.
 */
function CarouselImage(props) {
  let {imageSrc, currentIndex, name, component: Component} = props;

  return (
    <ul className="carousel-image">
      <Component className={name}>
        <img
          src={imageSrc[currentIndex]}
          key={imageSrc[currentIndex]}
          draggable={false}
        />
        {currentIndex === 0 && <h2>多样化的数据展示</h2>}
        {currentIndex === 0 && <p>系统支持包含客流,门前客流，新老客户，深访跳出等11项数据报表展示。</p>}
        {currentIndex === 1 && <h2>精准数据统计</h2>}
        {currentIndex === 1 && <p>结合离线与实时计算，数据统计更精准</p>}
        {currentIndex === 2 && <h2>使用便捷</h2>}
        {currentIndex === 2 && <p>仅需几分钟您便可学会该系统的使用方式。</p>}

      </Component>
    </ul>
  );
}

export default CarouselImage;
