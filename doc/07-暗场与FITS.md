# 暗场与 FITS

## 1. 暗场模式

`DarkMode` 包含三种状态：

| UI | 枚举 | 行为 |
| --- | --- | --- |
| normal | `Normal` | 不累加、不扣除 |
| dark | `Accumulate` | 把每帧加入暗场累计值 |
| subdark | `Subtract` | 从当前帧扣除暗场平均值 |

切换到 dark 时会调用 `clearDark()`，清零之前的累计结果和帧数。

## 2. 暗场累加

累计缓冲使用 `std::vector<double>`，长度与 3000×2000 Frame 相同。每个像素执行：

```text
darkAccumulator[i] += frame[i]
darkFrameCount += 1
```

使用 double 可以降低多帧累加时的溢出风险。按 600 万像素计算，累计缓冲约占 48 MB。

## 3. 暗场扣除

subdark 模式要求已经加载或累计至少一帧暗场：

```text
darkMean = round(darkAccumulator[i] / darkFrameCount)
corrected = frame[i] - darkMean + 500
frame[i] = clamp(corrected, 0, 65535)
```

500 是从旧工程继承的 pedestal，用于避免暗场扣除后大量像素落到零以下。扣除直接作用于 `ImageProcessor` 保存的 16 位 Frame，因此后续预览、统计和 FITS 都使用校正结果。

## 4. `.drk` 文件格式

当前格式兼容旧 Delphi `SaveDark/LoadDark`：

```text
offset  size                         content
0       4                            int32 darkFrameCount
4       3000×2000×4                  float32 darkAccumulator[]
```

字段使用宿主机原生端序。旧软件和当前主要目标都是小端 x86/x64，因此可直接互通。文件大小应为：

```text
4 + 6,000,000 × 4 = 24,000,004 bytes
```

注意：磁盘中保存的是每个像素的累计和，不是平均暗场。加载后仍需用首部帧数做除法。

### 格式限制

- 没有 magic、版本、宽高和校验；
- 不能判断文件属于哪个传感器尺寸；
- float32 可能比内存 double 损失少量精度；
- 原生端序不适合作为长期跨架构格式。

为保持旧文件兼容，当前没有改变格式。若未来设计 `.drk2`，建议增加 magic、版本、宽高、数据类型、端序和校验和。

## 5. 暗场文件操作

- `Save dark`：以 Output prefix 为路径，缺少 `.drk` 时自动追加；
- `Load dark...`：打开 ImGui 文件选择器，筛选 `.drk`；
- `View dark`：把平均暗场复制到当前 16 位 Frame，然后重建预览；
- 加载失败、文件截断或帧数无效时向日志写入异常。

`View dark` 不清除暗场累计值，只改变当前显示 Frame。

## 6. FITS 输出原则

FITS 直接写 16 位 `Frame::pixels`，不写 8 位 RGBA 预览。输出包含：

- 80 字节 header card；
- header 补齐到 2880 字节块；
- 16 位 big-endian 图像数据；
- data 补齐到 2880 字节块。

## 7. FITS Header

当前写入：

| Keyword | 含义 |
| --- | --- |
| `SIMPLE` | 标准 FITS |
| `BITPIX=16` | 16 位有符号存储单元 |
| `NAXIS=2` | 二维图像 |
| `NAXIS1` | 宽度，当前 3000 |
| `NAXIS2` | 高度，当前 2000 |
| `BSCALE=1` | 线性缩放 |
| `BZERO=32768` | 将有符号存储恢复为无符号 16 位 |
| `CBLACK` | 当前显示黑场 |
| `CWHITE` | 当前显示白场 |
| `EXPOSURE` | 曝光秒数 |
| `CCD-TEMP` | 帧记录的传感器温度 |
| `END` | Header 结束 |

## 8. 无符号 16 位到 FITS 的转换

FITS `BITPIX=16` 表示有符号 16 位，而相机数据为 `uint16`。写入前执行：

```text
stored = int16(pixel - 32768)
```

文件按大端写入。读取软件应用：

```text
physical = BZERO + BSCALE × stored
         = 32768 + stored
```

从而恢复原始 `[0,65535]` 范围。

## 9. 文件命名

自动和手动 FITS 使用：

```text
<Output prefix><frameNumber>.fit
```

例如 prefix 为 `M42_`、帧号为 12 时生成 `M42_12.fit`。启用 Write FITS 时帧号重置为 0；连续采集每处理完一帧后递增。

## 10. 当前 FITS 限制

- 没有日期、相机型号、bin、ROI 和 Bayer pattern 等 metadata；
- ROI 仍保存为完整 3000×2000，未采集区域为 0；
- bin 数据也扩展为 3000×2000 后保存；
- 没有压缩、多扩展或校验和；
- 写文件在 UI 完成帧处理路径中同步执行，大批量采集可考虑移到文件线程。

## 11. 代码位置

- 暗场：`src/image/ImageProcessor.cpp`
- FITS：`src/image/FitsWriter.cpp`
- UI 操作：`src/ui/ControlPanel.cpp`
- 自动保存：`src/ui/MainWindow.cpp`
- 旧格式参考：`cam86-view-old/ImCam.pas`

