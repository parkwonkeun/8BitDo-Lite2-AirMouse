package local.lite2.control;

import android.app.AlertDialog;
import android.content.Intent;
import android.provider.Settings;
import android.text.InputType;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Toast;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.*;
import java.util.concurrent.TimeUnit;

/** Start only this app's input helper through Android's user-paired wireless debugging. */
final class TabletInput {
    private final ControlActivity app;
    private boolean busy;
    TabletInput(ControlActivity app){this.app=app;}

    void show() {
        if(busy)return;
        String[] options={app.t("무선 디버깅 설정"),app.t("처음 연결 · 페어링"),app.t("입력 도우미 시작")};
        new AlertDialog.Builder(app).setTitle(app.t("태블릿에서 입력 도우미 연결")).setItems(options,(d,n)->{
            if(n==0){app.startActivity(new Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS));return;}
            form(n==1);
        }).show();
    }
    private void form(boolean pairing) {
        LinearLayout layout=new LinearLayout(app);layout.setOrientation(LinearLayout.VERTICAL);
        int pad=Math.round(24*app.getResources().getDisplayMetrics().density);
        layout.setPadding(pad,0,pad,0);
        EditText port=new EditText(app);port.setSingleLine();port.setInputType(InputType.TYPE_CLASS_NUMBER);
        port.setHint(app.t(pairing?"페어링 포트":"연결 포트"));layout.addView(port);
        EditText code=new EditText(app);code.setSingleLine();
        if(pairing) {
            code.setInputType(InputType.TYPE_CLASS_NUMBER|InputType.TYPE_NUMBER_VARIATION_PASSWORD);
            code.setHint(app.t("6자리 페어링 코드"));layout.addView(code);
        }
        AlertDialog dialog=new AlertDialog.Builder(app).setTitle(app.t(pairing?"처음 연결 · 페어링":"입력 도우미 시작"))
            .setMessage(app.t(pairing?"분할 화면에서 무선 디버깅의 ‘페어링 코드로 기기 페어링’을 열고 포트와 코드를 입력하세요.":"무선 디버깅 첫 화면의 ‘IP 주소 및 포트’에서 콜론 뒤 숫자를 입력하세요."))
            .setView(layout).setNegativeButton(android.R.string.cancel,null).setPositiveButton(android.R.string.ok,null).create();
        dialog.show();
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{
            String number=port.getText().toString(),secret=code.getText().toString();
            if(!number.matches("[0-9]{1,5}") || Integer.parseInt(number)<1 || Integer.parseInt(number)>65535){port.setError(app.t("포트를 확인하세요."));return;}
            if(pairing && !secret.matches("[0-9]{6}")){code.setError(app.t("6자리 페어링 코드"));return;}
            dialog.dismiss();busy=true;
            new Thread(()->start(pairing,number,secret),"tablet-input").start();
        });
    }
    private String command(String input,String... arguments) throws Exception {
        File dir=app.getDir("tablet_input",0);
        List<String> cmd=new ArrayList<>();
        cmd.add(new File(app.getApplicationInfo().nativeLibraryDir,"liblite2adb.so").toString());
        cmd.add("-L");cmd.add("localfilesystem:"+new File(dir,"adb.sock"));
        Collections.addAll(cmd,arguments);
        ProcessBuilder builder=new ProcessBuilder(cmd);
        Map<String,String> env=builder.environment();
        env.put("HOME",dir.toString());env.put("ANDROID_USER_HOME",dir.toString());
        env.put("TMPDIR",dir.toString());env.put("ADB_MDNS_AUTO_CONNECT","0");
        env.put("ANDROID_ADB_LOG_PATH",new File(dir,"server.log").toString());
        File log=File.createTempFile("result",".txt",dir);
        try {
            Process process=builder.redirectErrorStream(true).redirectOutput(log).start();
            try {
                try(OutputStream out=process.getOutputStream()){if(input!=null)out.write(input.getBytes(StandardCharsets.UTF_8));}
                if(!process.waitFor(22,TimeUnit.SECONDS))throw new IOException("Timeout");
                String result=new String(java.nio.file.Files.readAllBytes(log.toPath()),StandardCharsets.UTF_8);
                if(process.exitValue()!=0)throw new IOException("ADB "+process.exitValue());
                return result;
            } finally {if(process.isAlive())process.destroyForcibly();}
        } finally {log.delete();}
    }
    private void start(boolean pairing,String port,String code) {
        String token=null;
        boolean success=false;
        try {
            String address="127.0.0.1:"+port;
            if(pairing) {
                String response=command(code+"\n","pair",address);
                success=response.contains("Successfully paired");
            } else {
                String response=command(null,"connect",address);
                if(!response.contains("connected to "+address))throw new IOException("Connection failed");
                if(!command(null,"-s",address,"get-state").trim().equals("device"))throw new IOException("Unauthorized");
                byte[] bytes=new byte[16];new SecureRandom().nextBytes(bytes);
                StringBuilder hex=new StringBuilder();
                for(byte value:bytes)hex.append(String.format(Locale.ROOT,"%02x",value&255));
                String path="/data/local/tmp/lite2-pointer-bridge";
                String src=new File(app.getApplicationInfo().nativeLibraryDir,"liblite2input.so").toString();
                command(null,"-s",address,"push",src,path+".next");
                // All command text is fixed except the locally generated hex token.
                String script="set -e\np="+path+"\nf=/data/local/tmp/lite2-bridge.pid\n"+
                    "if [ -f $f ]; then n=$(cat $f); case $n in ''|*[!0-9]*) ;; *) "+
                    "if [ \"$(readlink /proc/$n/exe)\" = \"$p\" ]; then kill -TERM $n; sleep 1; fi;; esac; fi\n"+
                    "chmod 700 $p.next\nmv $p.next $p\nnohup setsid $p "+hex+" >/data/local/tmp/lite2-bridge.log 2>&1 </dev/null &\n"+
                    "n=$!; echo $n >$f; sleep 1; kill -0 $n; grep -q 'Waiting for Lite 2 Control' /data/local/tmp/lite2-bridge.log; echo LITE2_READY\n";
                if(!command(script,"-s",address,"shell","sh").contains("LITE2_READY"))throw new IOException("Helper failed");
                token=hex.toString();success=true;
            }
        } catch(Exception e){android.util.Log.w("Lite2Control","Tablet setup: "+e.getClass().getSimpleName());}
        finally {try {command(null,"kill-server");} catch(Exception ignored){}}
        final String value=token;final boolean ok=success;
        app.runOnUiThread(()->{
            busy=false;
            if(value!=null)app.useTabletInput(value);
            if(app.isDestroyed())return;
            Toast.makeText(app,app.t(ok?(pairing?"페어링 완료 · 연결 포트로 도우미를 시작하세요.":"입력 도우미 연결됨"):"무선 디버깅의 포트와 페어링 상태를 확인하세요."),Toast.LENGTH_LONG).show();
        });
    }
}
