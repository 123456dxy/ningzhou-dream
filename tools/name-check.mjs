// 从 JS 版 society.mjs 里导出姓名用字表，确保 C++ 版和 JS 版逐字一致。
import { socNPCName } from '../../michangsheng-demo/society.mjs';

// 直接用 JS 的 socNPCName 生成一批名字，C++ 版照着比对。
const names = [];
for (let i = 0; i < 12; i++) names.push(socNPCName(i));
console.log('前12个名字:', names.join(' '));
console.log('第 100 / 1000 / 2000 个:', socNPCName(100), socNPCName(1000), socNPCName(2000));
