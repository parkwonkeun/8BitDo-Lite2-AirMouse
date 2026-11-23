package local.lite2.control;
import java.lang.reflect.Field;
import java.util.Map;
import java.util.Arrays;
import java.util.Locale;

class WordsCheck {
    public static void main(String[] args) throws Exception {
        String[] order=Words.menu();
        if(order.length!=10 || !order[0].equals("English") || !order[1].equals("한국어") ||
            !order[2].equals("Français") || !order[3].equals("Deutsch"))
            throw new AssertionError(Arrays.toString(order));
        Field field=Words.class.getDeclaredField("TEXT");
        field.setAccessible(true);
        Map<?,?> entries=(Map<?,?>)field.get(null);
        for(Map.Entry<?,?> entry:entries.entrySet()) {
            String[] translations=(String[])entry.getValue();
            if(translations.length!=10) throw new AssertionError(entry.getKey());
            String key=(String)entry.getKey();
            for(String t:translations) {
                if(key.contains("%.2f") || key.contains("%+.2f")) String.format(Locale.ROOT,t,1f,2f,3f,4f);
                else if(key.contains("%d")) String.format(Locale.ROOT,t,20,35);
                else if(key.contains("%s")) String.format(Locale.ROOT,t,"S","Lite2");
            }
        }
        System.out.println(entries.size()+" strings verified in ten languages");
    }
}
