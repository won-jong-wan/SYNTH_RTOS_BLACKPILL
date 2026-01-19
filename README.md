## 🎥 Demo Videos

> 각 항목을 클릭하면 데모 영상으로 이동합니다.

### ✅ 빠른 보기 (Grid)

| 1) ADSR 증가 | 2) ADSR 감소 | 3) Resonance(Q) | 4) Octave 변경 |
|---|---|---|---|
| [▶️ Watch](v_adsr_up) | [▶️ Watch](v_adsr_down) | [▶️ Watch](v_resonance) | [▶️ Watch](v_octave) |

| 5) Cutoff 감소 | 6) 파형 차이 (Sine/Square/Saw) |
|---|---|
| [▶️ Watch](v_cutoff_down) | [▶️ Watch](v_wave_diff) |

---

### 📌 영상 링크 목록 (텍스트)

- ADSR 증가: [Open](v_adsr_up)  
- ADSR 감소: [Open](v_adsr_down)  
- Resonance(Q): [Open](v_resonance)  
- Octave 변경: [Open](v_octave)  
- Cutoff 감소: [Open](v_cutoff_down)  
- 파형 차이(Sine/Square/Saw): [Open](v_wave_diff)

---

<!-- ✅ 링크 정의 (여기만 바꾸면 위 모든 링크가 자동으로 바뀜) -->
[v_adsr_up]: https://github.com/user-attachments/assets/20490c06-4561-4c52-9833-793dc6b0e555
[v_adsr_down]: https://github.com/user-attachments/assets/7daee3bf-dd8c-44fe-a728-9777f0d2aacb
[v_resonance]: https://github.com/user-attachments/assets/dcbed712-60cd-4608-ac7b-2feb3794245e
[v_octave]: https://github.com/user-attachments/assets/e0f30bff-a481-4cd4-b910-f7195fdd4b93
[v_cutoff_down]: https://github.com/user-attachments/assets/9f5a5483-4b9e-445a-a331-8de48a920aaf
[v_wave_diff]: https://github.com/user-attachments/assets/84fd0f4a-1169-4cac-be41-eebe17c3d590




<!-- ========================= -->
<!--  RTOS Synth README (KOR)  -->
<!-- ========================= -->

# 🎹 RTOS 기반 임베디드 디지털 신디사이저 (STM32 BlackPill)

STM32 BlackPill(STM32F411)에서 **44.1kHz 실시간 오디오**를 생성하고,  
**키패드/로터리 입력**으로 **Pitch(옥타브), 파형, ADSR, LPF(Cutoff/Resonance), Volume**을 제어하는  
**RTOS 기반 디지털 신디사이저** 프로젝트입니다.

---

## ✅ 1. 프로젝트 개요

- **MCU**: STM32 BlackPill (STM32F411)
- **Audio Output**: I2S DAC (ex. UDA1334A)
- **UI Display**: TFT-LCD (ILI9341, SPI)
- **Input**
  - 4×4 Matrix Keypad: 노트/파형/옥타브
  - Rotary Encoder 2개: 파라미터 조절(ADSR/Filter/Volume)
- **SW**
  - FreeRTOS 기반 태스크 분리
  - I2S + DMA(Circular)로 오디오 지속 출력
  - DDS(NCO) + ADSR + IIR(2nd LPF) + Polyphony

---

## 🧩 2. 구성도 (Hardware / System)

> PPT 이미지 캡처본을 `assets/` 폴더에 넣고 아래 경로만 맞춰주세요.

![구성도](assets/ppt_block_diagram.png)

- 키패드: 노트 / 파형 / 옥타브 입력
- 로터리1: ADSR/Filter 편집 값 변경 + 버튼으로 항목 선택/모드 이동
- 로터리2: 볼륨 조절
- TFT-LCD: 현재 파라미터 및 파형/ADSR 시각화
- I2S DAC: 스피커/이어폰 출력

---

## 🔊 3. 오디오 신호 처리 흐름

### 3.1 신호 처리 개념 (ADSR / IIR LPF)
![신호 처리](assets/ppt_signal_processing.png)

- **Oscillator (DDS/NCO)**: LUT 기반 파형 생성 (Sine / Square / Saw)
- **ADSR Envelope**: NoteOn/NoteOff에 따른 진폭 변화
- **IIR Low-Pass Filter (2nd Order Biquad)**  
  - Cutoff(Fc): 음색 밝기/고조파 조절  
  - Resonance(Q): 컷오프 근처 강조(필터 “쏘는” 느낌)

### 3.2 오디오 파이프라인 (Polyphony)
![오디오 흐름도](assets/ppt_audio_flow.png)

- Keypad 입력 → NoteOn → Voice 할당
- Voice별: `Oscillator → ADSR`
- Mixer(Sum/Gain) → IIR LPF → I2S DMA 출력

---

## ⏱️ 4. RTOS 기반 실시간 설계 (DMA + TaskNotify)

![타이밍 다이어그램](assets/ppt_dma_ready_timing.png)

### 핵심 아이디어
- **DMA Half/Full Complete ISR**에서는 **최소 작업(Notify)**만 수행
- 오디오 합성/필터 처리는 **AudioTask**에서 수행 (deadline 내 완료)

### 동작 구조(요약)
- `HAL_I2S_TxHalfCpltCallback()` → AudioTask Notify (앞쪽 버퍼 갱신)
- `HAL_I2S_TxCpltCallback()` → AudioTask Notify (뒤쪽 버퍼 갱신)
- AudioTask:
  - DDS 파형 생성
  - ADSR 적용
  - Polyphony 믹싱
  - IIR LPF 적용
  - I2S 버퍼 채움

---

## 🎛️ 5. Pitch Control (DDS/NCO 방식)

![Pitch Control](assets/ppt_pitch_control.png)

- `tuning_word = f_target * 2^32 / F_sample`
- `phase_acc += tuning_word`
- `index = phase_acc >> SHIFT` 로 LUT 접근
- 샘플레이트: **F_sample = 44.1kHz**

---

## ✨ 6. 주요 기능 (Features)

### 🎼 (1) Keypad 기능
- 노트 입력 (C4~B4)
- 파형 선택 (Sine / Square / Saw)
- 옥타브 Up / Down

### 🎚️ (2) Rotary 기능
- **Rotary #1**: ADSR/Filter 파라미터 “선택된 항목” 값 변경
- **Rotary #1 버튼**: ADSR 항목 이동(A/D/S/R) 또는 Filter 항목 이동(Cutoff/Q)
- **Rotary #2**: Volume 조절

### 🎧 (3) DSP 기능
- **ADSR Envelope** (A/D/S/R 상태 머신)
- **2차 IIR LPF (Biquad)**: Cutoff / Resonance(Q)
- **Polyphony (멀티 보이스) + Voice Stealing**
- **TFT UI 시각화**: ADSR/파형 그래프, 파라미터 표시

---



## 🎥 8. 데모 영상 (Demo Videos)

> ✅ 데모 영상은 사용자가 직접 업로드한 뒤, 아래 링크만 교체해서 사용하면 됩니다.  
> 권장 폴더 구조: `assets/videos/`

### 8.1 영상 목록 (표)

| 기능 | 파일(예시) | 링크(교체) | 비고 |
|---|---|---|---|
| ADSR 증가(Attack/Decay/Sustain/Release) | `demo_adsr_increase.mp4` | [영상 보기](assets/videos/ADSR_증가.mp4) | 엔벨로프 변화 확인 |
| ADSR 감소 | `demo_adsr_decrease.mp4` | [영상 보기](assets/videos/demo_adsr_decrease.mp4) | Release/감쇠 확인 |
| Resonance 변화(Q 조절) | `demo_resonance.mp4` | [영상 보기](assets/videos/demo_resonance.mp4) | 공진 강조 확인 |
| Cutoff 감소(Fc Down) | `demo_cutoff_down.mp4` | [영상 보기](assets/videos/demo_cutoff_down.mp4) | 고역 감쇠 확인 |
| Octave 변경(Up/Down) | `demo_octave.mp4` | [영상 보기](assets/videos/demo_octave.mp4) | Pitch 스케일 확인 |
| 파형 차이(Sine/Square/Saw) | `demo_waveform_diff.mp4` | [영상 보기](assets/videos/demo_waveform_diff.mp4) | 파형/음색 비교 |

### 8.2 (선택) Video 태그로 넣기

> GitHub 환경에 따라 `<video>`가 안 보일 수 있어 링크도 같이 유지하는 것을 권장합니다.

<details>
<summary>ADSR 증가 데모 (펼치기)</summary>

<video src="assets/videos/demo_adsr_increase.mp4" controls muted width="800"></video>

- 링크: [demo_adsr_increase.mp4](assets/videos/ADSR_증가.mp4)

</details>

<details>
<summary>ADSR 감소 데모 (펼치기)</summary>

<video src="assets/videos/demo_adsr_decrease.mp4" controls muted width="800"></video>

- 링크: [demo_adsr_decrease.mp4](assets/videos/demo_adsr_decrease.mp4)

</details>

<details>
<summary>Resonance(Q) 변화 데모 (펼치기)</summary>

<video src="assets/videos/demo_resonance.mp4" controls muted width="800"></video>

- 링크: [demo_resonance.mp4](assets/videos/demo_resonance.mp4)

</details>

<details>
<summary>Cutoff(Fc) 감소 데모 (펼치기)</summary>

<video src="assets/videos/demo_cutoff_down.mp4" controls muted width="800"></video>

- 링크: [demo_cutoff_down.mp4](assets/videos/demo_cutoff_down.mp4)

</details>

<details>
<summary>Octave 변경 데모 (펼치기)</summary>

<video src="assets/videos/demo_octave.mp4" controls muted width="800"></video>

- 링크: [demo_octave.mp4](assets/videos/demo_octave.mp4)

</details>

<details>
<summary>파형 차이(Sine/Square/Saw) 데모 (펼치기)</summary>

<video src="assets/videos/demo_waveform_diff.mp4" controls muted width="800"></video>

- 링크: [demo_waveform_diff.mp4](assets/videos/demo_waveform_diff.mp4)

</details>

---

## 🧯 9. 트러블슈팅 (Troubleshooting)

### 9.1 내부 Pull-up이 정상 동작하지 않는 문제
- **증상**: 버튼/키 입력이 불안정하거나, 기대한 HIGH 상태가 유지되지 않음
- **원인**: 특정 핀에서 내부 Pull-up 동작 불량(보드/핀 특성)
- **해결**: 문제 핀을 다른 핀으로 변경하여 정상 동작 확인

### 9.2 (권장) 오디오 글리치(끊김) 이슈 체크 포인트
- **원인 후보**
  - AudioTask 처리 시간이 deadline(버퍼 갱신 시간) 초과
  - 디버그 `printf()`를 오디오 루프에서 과도하게 호출
  - LCD 전체 redraw를 너무 자주 수행
- **개선 방법**
  - ISR에서는 notify만, 연산은 Task로 분리 유지
  - 오디오 생성 루프에서 printf 제거/조건부 출력
  - UI는 dirty-flag로 “바뀐 부분만 갱신”

---

## 📌 10. 배운 점 (What I Learned)

- **실시간 시스템에서 ISR 최소화**의 중요성  
  → DMA ISR은 “알림만”, 연산은 Task에서 처리하여 지연/응답성 확보
- **DDS + LUT** 방식으로 MCU에서도 효율적인 파형 생성 가능
- **ADSR + LPF** 조합으로 “악기 같은 표현력”을 만들 수 있음
- UI/LCD, 입력 처리, 오디오 처리 병행을 위해 **RTOS 기반 구조 분리**가 매우 효과적

---




