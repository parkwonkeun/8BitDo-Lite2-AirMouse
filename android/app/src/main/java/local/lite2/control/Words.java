package local.lite2.control;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

final class Words {
    private static final String[] LANGUAGES={"한국어","简体中文","日本語","English","Français","Deutsch","Español","Italiano","Português","Русский"};
    private static final int[] ORDER={3,0,4,5,6,7,8,9,1,2};
    static final int COUNT=LANGUAGES.length;
    static int selection(int language) {
        for(int i=0;i<ORDER.length;i++) if(ORDER[i]==language) return i;
        return 0;
    }
    static int languageAt(int selection) {return ORDER[selection];}
    static String[] menu() {
        String[] list=new String[ORDER.length];
        for(int i=0;i<ORDER.length;i++) list[i]=LANGUAGES[ORDER[i]];
        return list;
    }
    private static final Map<String,String[]> TEXT=new HashMap<>();
    static {
        add("스틱 보정","摇杆校准","スティック補正","Stick calibration");
        add("최소 / 영점 / 최대","最小 / 中心 / 最大","最小 / 中心 / 最大","Minimum / center / maximum");
        add("영점 측정","测量中心","中心を測定","Measure center");
        add("최대 범위 측정","测量行程","可動範囲を測定","Measure full travel");
        add("적용","应用","適用","Apply");
        add("패드와 입력 도우미 연결을 확인하세요.","请检查手柄和输入助手的连接。","パッドと入力ヘルパーの接続を確認してください。","Check the controller and input helper connections.");
        add("스틱을 놓고 가만히 두세요.","松开摇杆，保持静止。","スティックを離して静止させてください。","Release the stick and leave it still.");
        add("스틱을 바깥 테두리를 따라 천천히 한 바퀴 돌려 주세요.","将摇杆沿外圈缓慢转一圈。","スティックを外周に沿ってゆっくり一周させてください。","Move the stick slowly once around its outer edge.");
        add("L3로 두 번째 LED를 켜세요.","按 L3 点亮第二盏灯。","L3で2番目のLEDを点灯してください。","Press L3 until the second LED lights.");
        add("태블릿에서 입력 도우미 연결","在平板上连接输入助手","タブレットで入力ヘルパーに接続","Connect input helper on tablet");
        add("무선 디버깅 설정","无线调试设置","ワイヤレス デバッグ設定","Wireless debugging settings");
        add("처음 연결 · 페어링","首次连接 · 配对","初回接続・ペア設定","First connection · pair");
        add("입력 도우미 시작","启动输入助手","入力ヘルパーを起動","Start input helper");
        add("페어링 포트","配对端口","ペア設定ポート","Pairing port");
        add("연결 포트","连接端口","接続ポート","Connection port");
        add("6자리 페어링 코드","6 位配对码","6桁のペア設定コード","6-digit pairing code");
        add("분할 화면에서 무선 디버깅의 ‘페어링 코드로 기기 페어링’을 열고 포트와 코드를 입력하세요.","在分屏中打开无线调试的配对码页面，输入端口和配对码。","分割画面でワイヤレスデバッグのペア設定コードを開き、ポートとコードを入力します。","In split screen, open Pair device with pairing code under Wireless debugging. Enter the port and code.");
        add("무선 디버깅 첫 화면의 ‘IP 주소 및 포트’에서 콜론 뒤 숫자를 입력하세요.","输入无线调试首页 IP 地址冒号后的端口。","ワイヤレスデバッグの「IPアドレスとポート」のコロンの後の数字を入力します。","On the main Wireless debugging screen, enter the number after the colon in IP address & port.");
        add("포트를 확인하세요.","请检查端口。","ポートを確認してください。","Check the port.");
        add("페어링 완료 · 연결 포트로 도우미를 시작하세요.","配对完成，请使用连接端口启动助手。","ペア設定完了。接続ポートでヘルパーを起動してください。","Paired. Start the helper using the connection port.");
        add("입력 도우미 연결됨","输入助手已连接","入力ヘルパー接続済み","Input helper connected");
        add("무선 디버깅의 포트와 페어링 상태를 확인하세요.","请检查无线调试的端口和配对状态。","ワイヤレスデバッグのポートとペア設定を確認してください。","Check the wireless debugging port and pairing status.");
        add("스틱 영점 보정","摇杆中心校准","スティック中心補正","Calibrate stick center");
        add("측정","测量","測定","Measure");
        add("L3로 두 번째 LED를 켜고 양쪽 스틱에서 손을 떼세요. 측정 후 D 모드 USB로 패드에 기록합니다.","按 L3 切到第二盏灯，松开两个摇杆。测量后通过 D 模式 USB 写入手柄。","L3で2番目のLEDを点灯させ、両スティックから手を離してください。測定後、DモードのUSBで書き込みます。","Press L3 until LED 2 lights and release both sticks. After measuring, write to the controller over USB in D mode.");
        add("측정값을 저장했습니다. D 모드 USB로 패드에 기록하면 RAW와 모든 슬롯에 적용됩니다.","测量值已保存。通过 D 模式 USB 写入后，RAW 和所有槽位均生效。","測定値を保存しました。DモードのUSBで書き込むとRAWと全スロットに適用されます。","Measurement saved. Write it over D-mode USB to apply it to RAW and all slots.");
        add("입력 도우미 연결과 두 번째 LED를 확인하고 스틱을 놓은 채 다시 측정하세요.","请确认输入助手已连接且第二盏灯亮起，松开摇杆后重试。","入力ヘルパーの接続と2番目のLEDを確認し、スティックを離して再測定してください。","Check the input helper and LED 2, release both sticks and measure again.");
        add("모니터 연결 대기 · 커서가 움직일 수 있습니다","等待监视连接 · 指针可能会移动","モニター接続待ち · カーソルが動く場合があります","Waiting for monitor connection · pointer may move");
        add("언어","语言","言語","Language");
        add("현재 슬롯 저장","保存当前配置","現在のスロットを保存","Save current slot");
        add("저장","保存","保存","Save");
        add("저장 안 함","不保存","保存しない","Discard");
        add("계속 편집","继续编辑","編集を続ける","Keep editing");
        add("변경 사항을 저장할까요?","保存更改吗？","変更を保存しますか？","Save changes?");
        add("저장하지 않으면 마지막으로 저장한 값으로 돌아갑니다.","不保存将恢复到上次保存的值。","保存しない場合、前回保存した値に戻ります。","Unsaved changes will be reverted.");
        add("저장하지 않은 변경 사항","未保存的更改","未保存の変更","Unsaved changes");
        add("다른 앱 위에 슬롯 표시","在其他应用上方显示配置","他のアプリの上にスロットを表示","Show slot over other apps");
        add("모니터 입력 수신","正在监测输入","入力をモニター中","Monitoring controller input");
        add("저장하지 않은 변경 사항 · 미리보기에 적용 중","未保存的更改 · 正在预览","未保存の変更 · プレビュー中","Unsaved changes · previewing");
        add("슬롯 %d의 저장값","配置 %d 的已保存值","スロット %d の保存値","Saved values for slot %d");
        add("슬롯 %d에 저장했습니다","已保存到配置 %d","スロット %d に保存しました","Saved to slot %d");
        add("슬롯 %d","配置 %d","スロット %d","Slot %d");
        add("패드의 기본 입력을 그대로 사용합니다.","直接使用手柄的默认输入。","コントローラーの標準入力をそのまま使用します。","Use the controller’s native input directly.");
        add("활성 슬롯 %d","当前配置 %d","使用中のスロット %d","Active slot %d");
        add("연결 대기 · 프로필을 미리 편집할 수 있습니다","等待连接 · 仍可编辑配置","接続待ち · プロファイルを編集できます","Waiting for a controller · profiles are editable");
        add("%s 모드 연결  •  %s","%s 模式已连接  •  %s","%s モード接続済み  •  %s","%s mode connected  •  %s");
        add("다른 모드 미리보기","正在预览另一模式","別のモードをプレビュー中","Previewing the other mode");
        add("S · 에어마우스","S · 空中鼠标","S · エアマウス","S · Air mouse");
        add("D · 게임패드","D · 手柄","D · ゲームパッド","D · Gamepad");
        add("모니터","监视","モニター","Monitor");
        add("설정","设置","設定","Settings");
        add("에어마우스 모니터","空中鼠标监视","エアマウスモニター","Air mouse monitor");
        add("게임패드 모니터","手柄监视","ゲームパッドモニター","Gamepad monitor");
        add("Android가 받은 마우스·키 입력입니다. 원시 스틱과 자이로 값은 장치 연결 기능이 필요합니다.","显示 Android 收到的鼠标和按键输入。原始摇杆和陀螺仪数据需要设备通信。","Android が受け取ったマウスとキー入力です。スティックとジャイロの生データには機器との通信が必要です。","Shows mouse and key input received by Android. Raw stick and gyro data require controller communication.");
        add("스틱을 움직여 원본 입력과 현재 프로필의 미리보기를 확인하세요.","移动摇杆，查看原始输入和当前配置的输出预览。","スティックを動かして生入力と現在の設定の出力プレビューを確認できます。","Move the sticks to compare raw input and profile output.");
        add("저장 슬롯","保存配置","保存スロット","Profile slots");
        add("모드별 슬롯 3개 · 우상단 저장 버튼으로 변경값을 기록합니다.","每种模式有 3 个配置 · 点击右上角保存更改。","各モードに 3 スロット · 右上の保存ボタンで変更を保存します。","Three slots per mode · tap Save to keep changes.");
        add("기본값","默认值","初期値","Defaults");
        add("저장 전 값","上次保存值","前回の保存値","Revert to saved");
        add("프로필 미리보기 · 현재 패드 펌웨어로 설정을 보내는 기능은 준비 중입니다.","配置预览 · 向手柄固件发送设置的功能尚未启用。","プロファイルプレビュー · パッドへの設定送信機能は準備中です。","Profile preview · sending settings to the controller is not available yet.");
        add("자이로","陀螺仪","ジャイロ","Gyroscope");
        add("좌우·상하 민감도와 자동 모드의 유지 시간을 별도로 설정합니다.","分别设置水平、垂直灵敏度和自动模式保持时间。","横・縦の感度と自動モードの保持時間を個別に設定します。","Set horizontal and vertical sensitivity and the auto-mode hold time.");
        add("클릭 시 자이로 고정 (0.2초)","点击时暂停陀螺仪（0.2 秒）","クリック時にジャイロを固定（0.2秒）","Steady gyro on click (0.2 s)");
        add("최대 감도","最大灵敏度","最大感度","Maximum sensitivity");
        add("좌우 감도","水平灵敏度","横感度","Horizontal sensitivity");
        add("상하 감도","垂直灵敏度","縦感度","Vertical sensitivity");
        add("자동 모드 스틱 유지","自动模式摇杆保持","自動モードでのスティック保持","Auto-mode stick hold");
        add(" × 0.1초"," × 0.1 秒"," × 0.1 秒"," × 0.1 s");
        add("가로축은 회전 속도, 세로축은 포인터 속도입니다.","横轴为转动速度，纵轴为指针速度。","横軸は回転速度、縦軸はポインター速度です。","Horizontal: rotation speed. Vertical: pointer speed.");
        add("버튼 할당","按键分配","ボタン割り当て","Button assignments");
        add("패드에서 설정할 버튼을 누르세요.","轻触手柄图上的按键进行设置。","設定するボタンをパッドの図でタップしてください。","Tap a button on the controller to assign it.");
        add("버튼을 고르고 동작을 선택하세요. 슬롯 전환은 모니터에서 미리 시험할 수 있습니다.","选择按键和操作，可在监视页测试配置切换。","ボタンと動作を選択します。モニターでスロット切り替えを試せます。","Choose a button and an action. Try slot switches in Monitor.");
        add("%s 동작","%s 操作","%s の動作","%s action");
        add("닫기","关闭","閉じる","Close");
        add("왼쪽 스틱","左摇杆","左スティック","Left stick");
        add("오른쪽 스틱","右摇杆","右スティック","Right stick");
        add("오른쪽 스틱 · 휠","右摇杆 · 滚轮","右スティック · ホイール","Right stick · wheel");
        add("가로축은 기울기, 세로축은 적용할 반응입니다.","横轴为摇杆输入，纵轴为响应。","横軸は傾き、縦軸は反応です。","Horizontal axis shows tilt; vertical axis shows response.");
        add("데드존","死区","デッドゾーン","Dead zone");
        add("감도","灵敏度","感度","Sensitivity");
        add("자이로 가속","陀螺仪加速","ジャイロ加速","Gyro acceleration");
        add("내부 점 %d개","内部点 %d 个","内側の点 %d 個","Interior points · %d");
        add("곡선 · %s","曲线 · %s","曲線 · %s","Curve · %s");
        add("%s 반응 곡선","%s 响应曲线","%s の反応曲線","%s response curve");
        add("기울기 테스트","测试倾斜","傾きのテスト","Test tilt");
        add("입력 %d%% → 출력 %d%%","输入 %d%% → 输出 %d%%","入力 %d%% → 出力 %d%%","Input %d%% → output %d%%");
        add("스틱 입력과 출력 곡선","摇杆输入和输出曲线","スティックの入出力曲線","Stick input and output curve");
        add("입력 대기 중","等待输入","入力待ち","Waiting for input");
        add("포인터 대기","等待指针输入","ポインター入力待ち","Waiting for pointer input");
        add("포인터 입력 수신","已收到指针输入","ポインター入力を受信","Pointer input received");
        add("왼쪽 X %+.2f Y %+.2f   오른쪽 X %+.2f Y %+.2f","左 X %+.2f Y %+.2f   右 X %+.2f Y %+.2f","左 X %+.2f Y %+.2f   右 X %+.2f Y %+.2f","Left X %+.2f Y %+.2f   Right X %+.2f Y %+.2f");
        add("%s · 입력","%s · 输入","%s · 入力","%s · input");
        add("할당 미리보기 · %s","分配预览 · %s","割り当てプレビュー · %s","Assignment preview · %s");
        add("앱에서 슬롯 %d 선택","已在应用中选择配置 %d","アプリでスロット %d を選択","Selected slot %d in the app");
        add("기본 입력","默认输入","標準入力","Default input");
        add("미할당","未分配","未割り当て","Unassigned");
        add("왼클릭","左键","左クリック","Left click");
        add("우클릭","右键","右クリック","Right click");
        add("가운데 클릭","中键","中クリック","Middle click");
        add("휠 위","向上滚动","上スクロール","Scroll up");
        add("휠 아래","向下滚动","下スクロール","Scroll down");
        add("뒤로","返回","戻る","Back");
        add("홈","主页","ホーム","Home");
        add("전원","电源","電源","Power");
        add("화살표 위","上箭头","上矢印","Up arrow");
        add("화살표 아래","下箭头","下矢印","Down arrow");
        add("화살표 왼쪽","左箭头","左矢印","Left arrow");
        add("화살표 오른쪽","右箭头","右矢印","Right arrow");
        add("십자키 위","方向键上","十字キー上","D-pad up");
        add("십자키 아래","方向键下","十字キー下","D-pad down");
        add("십자키 왼쪽","方向键左","十字キー左","D-pad left");
        add("십자키 오른쪽","方向键右","十字キー右","D-pad right");
        add("포인터 모드 전환","切换指针模式","ポインターモード切替","Switch pointer mode");
        add("볼륨 올리기","调高音量","音量を上げる","Volume up");
        add("볼륨 내리기","调低音量","音量を下げる","Volume down");
        add("%s · 뗌","%s · 已松开","%s · 離した","%s · released");
        add("이전 슬롯","上一个配置","前のスロット","Previous slot");
        add("다음 슬롯","下一个配置","次のスロット","Next slot");
        add("슬롯 1","配置 1","スロット 1","Slot 1");
        add("슬롯 2","配置 2","スロット 2","Slot 2");
        add("슬롯 3","配置 3","スロット 3","Slot 3");
        add("균형","均衡","バランス","Balanced");
        add("초반 빠르게","起步快","初動を速く","Fast start");
        add("초반 느리게","起步慢","初動をゆっくり","Slow start");
        add("직선","线性","リニア","Linear");
        add("직접 그리기","自定义","カスタム","Custom");
        for(String name:new String[]{"A","B","X","Y","LB","RB","LT","RT","L3","R3","Home"})
            add("게임패드 "+name,"手柄 "+name,"ゲームパッド "+name,"Gamepad "+name);
        add("연결 안 됨","未连接","未接続","Disconnected");
        add("편집 프로필","编辑配置","編集するプロファイル","Edit profile");
        add("편집 프로필 · %s","编辑配置 · %s","編集プロファイル · %s","Edit profile · %s");
        add("편집 슬롯 %d 미리보기","正在预览编辑中的配置 %d","編集中のスロット %d のプレビュー","Previewing edits to slot %d");
        add("이 슬롯 사용","使用此配置","このスロットを使用","Use this slot");
        add("Lite 2 커서 보정 연결","Lite 2 光标调整已连接","Lite 2 カーソル補正接続済み","Lite 2 cursor remapping connected");
        add("r13 연결 시 왼쪽 스틱은 기울기 곡선으로, 자이로는 좌우·상하 감도로 조절합니다. 자이로 가속을 켜면 빠르게 돌릴 때 이동량이 커집니다.","连接 r13 时，左摇杆使用倾斜曲线，陀螺仪使用水平和垂直灵敏度。启用加速后，快速转动时光标移动更远。","r13 接続時は左スティックに傾き曲線、ジャイロに縦横の感度を適用します。ジャイロ加速を有効にすると速く回したときの移動量が増えます。","With r13, the curve controls stick tilt and axis sensitivity controls gyro movement. Gyro acceleration increases travel on fast turns.");
        add("D 프로필 미리보기 · 설정이 게임패드 입력에 직접 적용되지 않습니다.","D 配置预览 · 设置不会直接更改手柄输入。","D プロファイルプレビュー · ゲームパッド入力には直接反映されません。","D profile preview · settings do not directly change gamepad input.");
        add("＋와 −를 함께 3초 누르면 설정이 열립니다.","同时按住 ＋ 和 − 3 秒，打开设置。","＋と−を同時に3秒間押すと設定が開きます。","Hold ＋ and − together for 3 seconds to open Settings.");
        add("LB와 RB를 함께 3초 누르면 앱 화면이 닫힙니다.","同时按住 LB 和 RB 3 秒，关闭应用界面。","LBとRBを同時に3秒間押すとアプリ画面が閉じます。","Hold LB and RB together for 3 seconds to close the app screen.");
        ExtraWords.addTo(TEXT);
    }
    private static void add(String ko,String zh,String ja,String en) {
        TEXT.put(ko,new String[]{ko,zh,ja,en});
    }
    static String get(int language,String key,Object... args) {
        String[] entry=TEXT.get(key);
        String pattern=entry==null?key:entry[language<entry.length?language:3];
        return args.length==0?pattern:String.format(Locale.ROOT,pattern,args);
    }
}

