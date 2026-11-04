
// 获取昨天今天日期，如：2017-04-19,2017-04-20
function getTime() {
  const now = new Date();
  const year = now.getFullYear();
  const month = (now.getMonth() + 1);
	// month=month>=10?month:'0'+month;
  const day = now.getDate();
	// day=day>=10?day:'0'+day;
  const time1 = `${year}-${month}-${day - 1}`;
  const time2 = `${year}-${month}-${day}`;
  return `${time1},${time2}`;
}
// 时间格式，补0。如：2017-4-12=>2017-04-12
function changeTime(time) {
	// 两个时间，如2017-4-12,2017-1-2
  if (!time) { return; }
  if (time.indexOf(',') > -1) {
    const arr = time.split(',');
    const time1 = changeTime(arr[0]);
    const time2 = changeTime(arr[1]);
    return `${time1},${time2}`;
  }
  const arr =	time.split('-');
  arr[1] = arr[1] >= 10 ? arr[1] : `0${arr[1]}`;
  arr[2] = arr[2] >= 10 ? arr[2] : `0${arr[2]}`;
  return arr.join('-');
}

/* A screen's own copy of the option it is about to hand to echarts.

   Every chart used to call `.toJS()` on the Immutable map it selected, which
   returned a fresh plain object each time, and several of them then edited it -
   pushing legend entries, naming a series, reversing an axis - before calling
   setOption.  Slice state is plain objects now, so selecting one hands back the
   object that is in the store; editing that would corrupt the store and, with
   the development-time freeze on, throw.  This is the `.toJS()` those screens
   were relying on.  Safe as a plain deep copy because these are echarts
   options: data, never functions. */
function copyOption(option) {
  return structuredClone(option);
}


export default {getTime, changeTime, copyOption};

