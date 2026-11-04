import React, {useEffect, useRef, useState} from 'react';
import '../styles/calendar.scss';
import { FaAngleDoubleLeft, FaAngleDoubleRight, FaAngleLeft, FaAngleRight } from 'react-icons/fa';

const displayDaysPerMonth = (year) => {
  // 定义每个月的天数，如果是闰年第二月改为29天
  const daysInMonth = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  if ((year % 4 === 0 && year % 100 !== 0) || year % 400 === 0) {
    daysInMonth[1] = 29;
  }
  // 以下为了获取一年中每一个月在日历选择器上显示的数据，
  // 从上个月开始，接着是当月，最后是下个月开头的几天
  // 定义一个数组，保存上一个月的天数
  const daysInPreviousMonth = [].concat(daysInMonth);
  daysInPreviousMonth.unshift(daysInPreviousMonth.pop());
  // 获取每一个月显示数据中需要补足上个月的天数
  const addDaysFromPreMonth = new Array(12)
    .fill(null)
    .map((item, index) => {
      const day = new Date(year, index, 1).getDay();
      if (day === 0) {
        return 6;
      }
      return day - 1;
    });
  // 已数组形式返回一年中每个月的显示数据,每个数据为6行*7天
  return new Array(12)
    .fill([])
    .map((month, monthIndex) => {
      let addDays = addDaysFromPreMonth[monthIndex],
        daysCount = daysInMonth[monthIndex],
        daysCountPrevious = daysInPreviousMonth[monthIndex],
        monthData = [];
      // 补足上一个月
      for (; addDays > 0; addDays--) {
        monthData.unshift(daysCountPrevious--);
      }
      // 添入当前月
      for (let i = 0; i < daysCount;) {
        monthData.push(++i);
      }
      // 补足下一个月
      for (let i = 42 - monthData.length, j = 0; j < i;) {
        monthData.push(++j);
      }
      return monthData;
    });
};


function Calendar() {
  /* this.state was an instance field as much as it was state.  Every setState in
     here was preceded by a write straight onto the object (`--this.state.year`),
     the picked cell was parked on it without a setState at all, and 今天 replaced
     the whole object without asking for a render - so the header kept showing the
     previously picked date while the input showed today's.  The mutable object
     stays, because the deferred writes below have to read the latest values
     rather than the ones the render that installed them closed over, and
     because 今天 still must not repaint.  What changes is that render no longer
     reads it: `view` is a copy published at exactly the points that used to ask
     for a render, so the three fields the header and the grid read are state and
     the object behind them is never touched during a render. */
  const [initialState] = useState(() => {
    const now = new Date();
    return {
      year: now.getFullYear(),
      month: now.getMonth(),
      day: now.getDate(),
      prevEl: ''
    };
  });
  const state = useRef(initialState);
  const [view, setView] = useState(() => ({
    year: initialState.year,
    month: initialState.month,
    day: initialState.day
  }));
  const render = () => setView({
    year: state.current.year,
    month: state.current.month,
    day: state.current.day
  });

  const timeInput = useRef(null);
  const timePicker = useRef(null);
  const tbody = useRef(null);

  /* The listener was added and never taken off.  It reads the ref every time it
     fires, and after this calendar is gone that ref is null and the body is a
     no-op, so letting the effect take it off again is the same window. */
  useEffect(() => {
    const hide = (e) => {
    		if (timePicker.current) {
    			timePicker.current.style.display = 'none';
    		}
    	};
    window.addEventListener('click', hide);
    timeInput.current.value = `${state.current.year}-${state.current.month + 1}-${state.current.day}`;
    return () => {
      window.removeEventListener('click', hide);
    };
  }, []);

	  // 切换到上一年
	  const prevYear = (e) => {
	      state.current.year -= 1;
	      render();
	    e.stopPropagation();
	  };
	  // 切换到下一年
	  const nextYear = (e) => {
	      state.current.year += 1;
	      render();
	    e.stopPropagation();
	  };
	  // 切换到上一个月
	  const prevMonth = (e) => {
	    if (state.current.month === 0) {
	      state.current.year -= 1;
	      state.current.month = 11;
	      render();
	    } else {
	      state.current.month -= 1;
	      render();
	    }
	    e.stopPropagation();
	  };
	  // 切换到下一个月
	  const nextMonth = (e) => {
	    if (state.current.month === 11) {
	      state.current.year += 1;
	      state.current.month = 0;
	      render();
	    } else {
	      state.current.month += 1;
	      render();
	    }
	    e.stopPropagation();
	  };
	  // 选择日期
	  const datePick = (e) => {
	  	// 全部去掉picked类名
	  	const allTr = e.target.parentNode.parentNode.children;
	  	for (const i in allTr) {
	  		if (i == 'length') break;
	  		const allTd = allTr[i].children;
	  		for (const j in allTd) {
	  			if (j == 'length') break;
	  			// console.log(allTd[j])
	  			// console.log(allTd[j].className)
	  			if (allTd[j].className.indexOf(' picked') !== -1) {
		  			allTd[j].className = allTd[j].className.replace(' picked', '');
	  			}
	  		}
	  	}


	  	// e.target.parentNode.parentNode.children.forEach((tr)=>{
	  	// 	tr.forEach((td)=>{
	  	// 		if(td.className.indexOf(' picked') !== -1){
	  	// 			td.className = td.className.replace(' picked','');

	  	// 		}
	  	// 	})
	  	// })
	  	if (e.target.className.indexOf('thisMonth') !== -1) {   // 这个月
	  		// if(e.target.className.indexOf('picked') !== -1) {return}
	  			// if(state.current.prevEl){
	  				// state.current.prevEl.className=state.current.prevEl.className.replace(' picked','');
	  			// }
	  		e.target.className = `${e.target.className} picked`;
	  		state.current.prevEl = e.target;
	  	} else if (e.target.innerText > 20) {      // 上个月
    prevMonth(e);
    state.current.prevEl = e.target;
    const day = e.target.innerText;
    timePicker.current.style.display = 'none';
	  	} else {       // 下个月
	  		state.current.prevEl = e.target;
    nextMonth(e);
    timePicker.current.style.display = 'none';
	  	}
    setTimeout(() => {
  			timeInput.current.value = `${state.current.year}-${state.current.month + 1}-${state.current.day}`;
    }, 0);
	    state.current.day = e.target.innerText;
	    render();

	    // e.stopPropagation();
	  };
	  const show = (e) => {
	  	const calendar = document.getElementsByClassName('calendar');
	  	const timePicker1 = calendar[0].getElementsByClassName('timePicker')[0];
	  	const timePicker2 = calendar[1].getElementsByClassName('timePicker')[0];
  		timePicker1.style.display = 'none';
  		timePicker2.style.display = 'none';
	  	// if(calendar[0]){
	  	// 	calendar.style.display = 'none';
	  	// }
	  	timePicker.current.style.display = 'block';

	  	// //显示今日picked类名,用上一个保存的兄弟节点
	  	// if(state.current.prevEl){
				// console.log(state.current.prevEl.parentNode.parentNode.children);
				// let allTr=state.current.prevEl.parentNode.parentNode.children;
		  // 	for(let i in allTr){
		  // 		if(i == 'length') break;
		  // 		let allTd=allTr[i].children;
		  // 		for(let j in allTd){
		  // 			if(j == 'length') break;
		  // 			// console.log(allTd[j])
		  // 			// console.log(allTd[j].className)
		  // 			if(allTd[j].className.indexOf(' picked') !== -1){
			 //  			allTd[j].className = allTd[j].className.replace(' picked','');
		  // 			}
		  // 		}
		  // 	}
	  	// }

	  	e.stopPropagation();
	  };

	  // 选择今天日期
	  const PickTOday = () => {
	  	const now = new Date();
    state.current = {
      year: now.getFullYear(),
			      month: now.getMonth(),
			      day: now.getDate()
    };
    setTimeout(() => {
	  			timeInput.current.value = `${state.current.year}-${state.current.month + 1}-${state.current.day}`;
    }, 0);
        // 点击今天，picked的类名去掉
    const allTr = tbody.current.children;
    for (const i in allTr) {
	  		if (i == 'length') break;
	  		const allTd = allTr[i].children;
	  		for (const j in allTd) {
	  			if (j == 'length') break;
	  			if (allTd[j].className.indexOf(' picked') !== -1) {
		  			allTd[j].className = allTd[j].className.replace(' picked', '');
	  			}
	  		}
	  	}
	  };


    	const month = view.month;  // 当月序号
    	const viewData = displayDaysPerMonth(view.year); // 一年的数据
    	const rowsInMonth = [[], [], [], [], [], []];
    	viewData[month] = viewData[month].map((item, index) => {   // 当月数值数据变成对象数据，用来保存className
    		const data = {};
    		data.data = item;
    		data.className = 'thisMonth';
    		return data;
    	});
    	let isThisMonth = false;
    	viewData[month].forEach((item, index) => {   // 赋className是否当月的值
    		if (item.data === 1) {
    			isThisMonth = !isThisMonth;
    			item.className = isThisMonth ? 'thisMonth' : 'notThisMonth';
    		} else if (!isThisMonth && item.data !== 1) {
    			item.className = 'notThisMonth';
    		} else {
    			item.className = 'thisMonth';
    		}
    	});
    	viewData[month].forEach((item, index) => {   // 赋行数的值
    			const rowIndex = parseInt(index / 7);
    			rowsInMonth[rowIndex].push(item);
    	});
    	const showData =
			      		rowsInMonth.map((row, rowIndex) => (<tr key={rowIndex}>
  {
			      					row.map((item, index) => <td key={`${rowIndex}${index}`} className={item.className} onClick={datePick}>{item.data}</td>)
			      				}
			      				</tr>));
    	return (<div className="calendar">
      <input placeholder="请选择日期" className="timeInput" ref={timeInput} onClick={show} />
      <div className="timePicker" ref={timePicker}>
        <div className="timePickerHeader">
          <FaAngleDoubleLeft className="floatLeft calendarDoubleLeft" onClick={prevYear} /> <FaAngleLeft className="floatLeft calendarLeft" onClick={prevMonth} /> {view.year}年{view.month + 1}月{view.day}日                                                                                                               <FaAngleDoubleRight className="floatRight calendarDoubleRight" onClick={nextYear} /><FaAngleRight className="floatRight calendarRight" onClick={nextMonth} />
        </div>
        <div className="timePickerBody">
          <table>
            <thead>
              <tr>
                <th>一</th>
                <th>二</th>
                <th>三</th>
                <th>四</th>
                <th>五</th>
                <th>六</th>
                <th>日</th>
              </tr>
            </thead>
            <tbody ref={tbody}>
              {showData}
            </tbody>
          </table>
        </div>
        <div className="timePickerFooter" onClick={PickTOday}>今天</div>
      </div>
    </div>);
}


/*
		      			*<tr>
		      				<td>27</td>
		      				<td>28</td>
		      				<td>29</td>
		      				<td>30</td>
		      				<td>31</td>
		      				<td>1</td>
		      				<td>2</td>
		      			</tr>
		      			<tr>
		      				<td>3</td>
		      				<td>4</td>
		      				<td>5</td>
		      				<td>6</td>
		      				<td>7</td>
		      				<td>8</td>
		      				<td>9</td>
		      			</tr>
		      	 		<tr>
		      				<td>10</td>
		      				<td>11</td>
		      				<td>12</td>
		      				<td>13</td>
		      				<td>14</td>
		      				<td>15</td>
		      				<td>16</td>
		      			</tr>
		       			<tr>
		      				<td>17</td>
		      				<td>18</td>
		      				<td>19</td>
		      				<td>20</td>
		      				<td>21</td>
		      				<td>22</td>
		      				<td>23</td>
		      			</tr>
		      			<tr>
		      				<td>24</td>
		      				<td>25</td>
		      				<td>26</td>
		      				<td>27</td>
		      				<td>28</td>
		      				<td>29</td>
		      				<td>30</td>
		      			</tr>
	      		 		<tr>
		      				<td>1</td>
		      				<td>2</td>
		      				<td>3</td>
		      				<td>4</td>
		      				<td>5</td>
		      				<td>6</td>
		      				<td>7</td>
		      			</tr>*/

export default Calendar;
