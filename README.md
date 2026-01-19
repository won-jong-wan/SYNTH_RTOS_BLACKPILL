<!-- ========================= -->
<!--  RTOS Synth README (KOR)  -->
<!-- ========================= -->

# 🎹 RTOS 기반 디지털 신디사이저 

<table>
  <tr>
    <td width="45%" valign="top">
      <img src="https://github.com/user-attachments/assets/4f4dce32-1f35-45e3-bfb7-9151e01d296c" width="100%" alt="Project Photo">
    </td>
    <td width="55%" valign="top">
      <h3>RTOS 기반 디지털 신디사이저</h3>
      <p>
        STM32 BlackPill(STM32F411)에서 <b>44.1kHz 실시간 오디오</b>를 생성하고,<br/>
        <b>키패드/로터리 입력</b>으로 <b>Pitch(옥타브)</b>, <b>파형</b>, <b>ADSR</b>, <b>LPF(Cutoff/Resonance)</b>, <b>Volume</b>을 제어하는
        RTOS 기반 디지털 신디사이저</b> 프로젝트입니다.
      </p>
      <ul>
        <li><b>Audio</b>: I2S + DMA(Circular), DDS + ADSR + IIR LPF</li>
        <li><b>UI</b>: ILI9341 TFT LCD (SPI)</li>
        <li><b>Input</b>: 4×4 Keypad + Rotary Encoder ×2</li>
      </ul>
    </td>
  </tr>
</table>

---

## ✨ 1. 주요 기능 (Features)

### 🎼 (1) Keypad 기능
- 음도 입력 (도,레,미,파,솔,라,시)
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

## 🧩 2. 구성도 (Hardware / System)

<img width="1080" height="522" alt="image" src="https://github.com/user-attachments/assets/3e9757d5-a652-40d4-9eae-89c765891a52" />



- 키패드: 노트 / 파형 / 옥타브 입력
- 로터리1: ADSR/Filter 편집 값 변경 + 버튼으로 항목 선택/모드 이동
- 로터리2: 볼륨 조절
- TFT-LCD: 현재 파라미터 및 파형/ADSR 시각화
- I2S DAC: 스피커/이어폰 출력

---


## 🎛️ 3. 파형생성 (DDS/NCO 방식)
<img width="947" height="226" alt="image" src="https://github.com/user-attachments/assets/815fcfed-07cf-402f-8126-cc65ab282f54" />

<img width="1034" height="388" alt="image" src="https://github.com/user-attachments/assets/300af78c-eeaa-4d9a-9251-0a03d904c974" />

- `tuning_word = f_target * 2^32 / F_sample`
- `phase_acc += tuning_word`
- `index = phase_acc >> SHIFT` 로 LUT 접근
- 샘플레이트: **F_sample = 44.1kHz**

---

## 🔊 4. 오디오 신호 처리 흐름

### 4.1 신호 처리 개념 (ADSR / IIR LPF)

<table>
  <tr>
    <th align="center">ADSR</th>
    <th align="center">Cutoff (LPF)</th>
    <th align="center">Resonance (Q)</th>
  </tr>
  <tr>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/5211d62c-eb23-4a60-aaa6-2dedb4405495" width="300" alt="ADSR GIF">
    </td>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/d9d8266c-0277-4fc1-8093-e0784a2a1aa7" width="300" alt="Cutoff GIF">
    </td>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/124efc4b-c8be-4dd5-984f-b19631db45e2" width="300" alt="Resonance GIF">
    </td>
  </tr>
</table>


- **ADSR Envelope**: NoteOn/NoteOff에 따른 진폭 변화
- **IIR Low-Pass Filter (2nd Order Biquad)**  
  - Cutoff(Fc): 음색 밝기/고조파 조절  
  - Resonance(Q): 컷오프 근처 강조(필터 “쏘는” 느낌)

### 4.2 오디오 파이프라인 (Polyphony)
<img width="1054" height="220" alt="image" src="https://github.com/user-attachments/assets/3f2feef0-8bda-4a0f-a028-e198c93cb1a8" />


- Keypad 입력 → NoteOn → Voice 할당
- Voice별: `Oscillator → ADSR`
- Mixer(Sum/Gain) → IIR LPF → I2S DMA 출력

---


## 5. I2S DMA Circular Buffer (Half/Full Ping-Pong)

<p align="center">
  <img src="https://github.com/user-attachments/assets/a3f5b8b1-4708-43e4-85bc-8e87ec9a8479" width="700" alt="image">

</p>

### 개념
- I2S는 **DMA Circular mode**로 오디오 버퍼를 반복 전송합니다.
- DMA는 버퍼를 **읽어서(READ)** DAC로 보내고, CPU(AudioTask)는 비는 구간을 **채워서(WRITE)** 끊김 없이 재생합니다.

### 동작 흐름
- **Half Complete ISR**: 버퍼 **앞쪽(1/2)** 전송 완료 → CPU가 **앞쪽을 채움**
- **Full Complete ISR**: 버퍼 **뒤쪽(1/2)** 전송 완료 → CPU가 **뒤쪽을 채움**
- ISR에서는 연산 최소화(Notify만) → 실제 오디오 생성은 **AudioTask**에서 수행

### 장점
- 샘플레이트(예: **44.1kHz**)를 일정하게 유지 → **실시간성 확보**
- CPU는 “반쪽 버퍼 deadline”만 맞추면 됨 → 안정적 오디오 출력
- 입력/UI 작업이 있어도 오디오 스트림이 끊기지 않도록 RTOS로 역할 분리 가능


---

## ⏱️ 6. RTOS 기반 실시간 설계 (DMA + TaskNotify)

<img width="1012" height="247" alt="image" src="https://github.com/user-attachments/assets/23aa3238-bf88-47d1-8525-193ec3a1b064" />

---


## 🎥 7. 데모 영상 (Demo Videos)

| ADSR 증가 | ADSR 감소 | Resonance(Q) | Octave 변경 |
|---|---|---|---|
| [▶️ Watch](https://github.com/user-attachments/assets/20490c06-4561-4c52-9833-793dc6b0e555) | [▶️ Watch](https://github.com/user-attachments/assets/7daee3bf-dd8c-44fe-a728-9777f0d2aacb) | [▶️ Watch](https://github.com/user-attachments/assets/dcbed712-60cd-4608-ac7b-2feb3794245e) | [▶️ Watch](https://github.com/user-attachments/assets/e0f30bff-a481-4cd4-b910-f7195fdd4b93) |


| Cutoff 감소 | 파형 차이 (Sine/Square/Saw) |
|---|---|
| [▶️ Watch](https://github.com/user-attachments/assets/9f5a5483-4b9e-445a-a331-8de48a920aaf) | [▶️ Watch](https://github.com/user-attachments/assets/84fd0f4a-1169-4cac-be41-eebe17c3d590) |

---


## 🧯 8. 트러블슈팅 (Troubleshooting)

<table>
  <tr>
    <td width="33%" valign="top">

### ⌨️ 스위치 입력 문제
- **현상**: Pull-up 저항 동작 안함  
- **원인**: MCU 내부 Pull-up 저항(해당 핀) 문제  
- **해결**: **MCU 핀 위치 변경**으로 정상 동작 확인

    </td>
    <td width="33%" valign="top">

### 🔁 버퍼 언더런 
- **현상**: 일정 주기로 짧게 소리가 끊김  
- **원인**: 샘플 생성 루프 내 **과도한 연산**으로 인해 I2S DMA 버퍼를 제때 채우지 못함  
- **해결**: 반복 수행되던 계산을 **루프 외부로 이동**하여 경로 최적화 → DMA 버퍼 안정적으로 갱신

    </td>
    <td width="33%" valign="top">

### 🖥️ LCD SPI 통신 오류
- **현상**: LCD 화면이 간헐적으로 꺼짐/깨짐  
- **원인**: LCD 라이브러리에서 **SPI 타임아웃(1ms)** 설정 → 타임아웃 부족으로 전송 실패  
- **해결**: 타임아웃을 **HAL_MAX_DELAY**로 변경(전송 완료까지 대기) → 화면 안정화

    </td>
  </tr>
</table>


---

## 📌 9. 배운 점 (What I Learned)

- **실시간 시스템에서 ISR 최소화**의 중요성  
  → DMA ISR은 “알림만”, 연산은 Task에서 처리하여 지연/응답성 확보
- **DDS + LUT** 방식으로 MCU에서도 효율적인 파형 생성 가능
- **ADSR + LPF** 조합으로 “악기 같은 표현력”을 만들 수 있음
- UI/LCD, 입력 처리, 오디오 처리 병행을 위해 **RTOS 기반 구조 분리**가 매우 효과적

---




