// 精确统计 JS 版姓名用字表的长度与内容，用于生成 C++ 常量。
const surnames = ['苏','谢','陆','顾','柳','沈','江','林','孟','裴','温','许','周','秦','楚','叶','方','宋','韩','洛','唐','萧','尹','白','程','纪','齐','薛','莫','季','徐','段','颜','凌','夏','穆','傅','燕','阮','宁','司马','上官','欧阳','慕容','司徒','南宫','闻人','公孙','东方','独孤'];
const first = [...'清云怀映照闻长明玉青行若观寄疏望修静昭玄松灵听归寒秋远鹤临知墨雨雪月星风竹苍锦南丹安景'];
const second = [...'川山宁和秋桐澜溪远衡舟岚之水南风寒月影微河舒阳尘玉声明迟晏枝安照均生雨鸿初白庭渊容书心歌尧真辞归溪雪华'];

console.log('surnames:', surnames.length);
console.log('first:', first.length);
console.log('second:', second.length);
console.log('space:', first.length * second.length);
console.log('firstJson:', JSON.stringify(first));
console.log('secondJson:', JSON.stringify(second));
console.log('surnamesJson:', JSON.stringify(surnames));
