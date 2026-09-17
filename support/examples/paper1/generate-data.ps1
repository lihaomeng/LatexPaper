param()
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$culture = [Globalization.CultureInfo]::InvariantCulture
function F([double]$v, [int]$digits=4) { $v.ToString("F$digits", $culture) }
function Save-Generated([string]$relative, [string]$content) {
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot $relative), $content, [Text.UTF8Encoding]::new($false))
}
foreach ($directory in @('data','figures')) {
    New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot $directory) -Force | Out-Null
}
$rows = @(1..120 | ForEach-Object {
    $i = $_
    $baseline = 20 + 2 * [Math]::Sin($i / 8.0)
    $label = [int]($i % 10 -eq 0)
    $observed = $baseline + 0.6 * [Math]::Sin(1.7*$i) + 2*$label + 1.6*[int]($i % 17 -eq 0)
    $residual = [Math]::Abs($observed-$baseline)
    [pscustomobject]@{i=$i; baseline=$baseline; observed=$observed; residual=$residual; label=$label;
        A=[int]($observed -gt 22); B=[int]($residual -gt 1.8); C=[int]($residual -gt 1.55)}
})
$csv = @('index,baseline,observed,residual,label,A,B,C')
foreach ($r in $rows) { $csv += (@($r.i,(F $r.baseline 6),(F $r.observed 6),(F $r.residual 6),$r.label,$r.A,$r.B,$r.C) -join ',') }
Save-Generated 'data/samples.csv' ($csv -join "`n")
$stats = @(foreach ($model in @('A','B','C')) {
    $tp=0; $tn=0; $fp=0; $fn=0
    foreach ($r in $rows) {
        if ($r.label -eq 1) { if ($r.$model -eq 1) { $tp++ } else { $fn++ } }
        else { if ($r.$model -eq 1) { $fp++ } else { $tn++ } }
    }
    if ($tp+$fn -ne 12 -or $tn+$fp -ne 108) { throw 'Confusion matrix invariant failed' }
    [pscustomobject]@{model=$model; TP=$tp; TN=$tn; FP=$fp; FN=$fn;
        Accuracy=($tp+$tn)/120.0; Precision=$(if ($tp+$fp) {$tp/($tp+$fp)} else {0});
        Recall=$tp/12.0; F1=$(if (2*$tp+$fp+$fn) {2*$tp/(2*$tp+$fp+$fn)} else {0})}
})
$metricCsv = @('model,TP,TN,FP,FN,accuracy,precision,recall,F1')
$metricTex = @('\begin{table}[htbp]\centering','\caption{合成数据上的检测指标}\label{tab:metrics}',
    '\begin{tabular}{lrrrr}\toprule','规则 & Accuracy & Precision & Recall & $F_1$\\\midrule')
$confusionTex = @('\begin{table}[htbp]\centering','\caption{可独立复算的混淆矩阵计数}\label{tab:confusion}',
    '\begin{tabular}{lrrrr}\toprule','规则 & TP & TN & FP & FN\\\midrule')
foreach ($s in $stats) {
    $values = @((F $s.Accuracy),(F $s.Precision),(F $s.Recall),(F $s.F1))
    $metricCsv += (@($s.model,$s.TP,$s.TN,$s.FP,$s.FN) + $values) -join ','
    $metricTex += ((@($s.model) + $values) -join ' & ') + '\\'
    $confusionTex += (@($s.model,$s.TP,$s.TN,$s.FP,$s.FN) -join ' & ') + '\\'
}
$metricTex += '\bottomrule\end{tabular}\end{table}'
$confusionTex += '\bottomrule\end{tabular}\end{table}'
Save-Generated 'data/metrics.csv' ($metricCsv -join "`n")
Save-Generated 'data/metrics-table.tex' ($metricTex -join "`n")
Save-Generated 'data/confusion-table.tex' ($confusionTex -join "`n")
$sample = @('\begin{table}[htbp]\centering\small','\caption{前十二个观测点}\label{tab:sample}',
    '\begin{tabular}{rrrrrrrr}\toprule','$i$ & $b_i$ & $x_i$ & $r_i$ & $y_i$ & A & B & C\\\midrule')
$full = @('\small','\begin{longtable}{rrrrrrrr}',
    '\caption{全部 120 条合成观测及预测}\label{tab:full}\\',
    '\toprule $i$ & $b_i$ & $x_i$ & $r_i$ & $y_i$ & A & B & C\\\midrule\endfirsthead',
    '\toprule $i$ & $b_i$ & $x_i$ & $r_i$ & $y_i$ & A & B & C\\\midrule\endhead',
    '\midrule\multicolumn{8}{r}{续下页}\\\endfoot','\bottomrule\endlastfoot')
foreach ($r in $rows) {
    $line = (@($r.i,(F $r.baseline 3),(F $r.observed 3),(F $r.residual 3),$r.label,$r.A,$r.B,$r.C) -join ' & ') + '\\'
    $full += $line
    if ($r.i -le 12) { $sample += $line }
}
$sample += '\bottomrule\end{tabular}\end{table}'
$full += '\end{longtable}\normalsize'
Save-Generated 'data/sample-table.tex' ($sample -join "`n")
Save-Generated 'data/full-table.tex' ($full -join "`n")

# Deterministic scientific figures; no external image service or plotting package.
$font = [Drawing.Font]::new('Arial',18)
$titleFont = [Drawing.Font]::new('Arial',25,[Drawing.FontStyle]::Bold)
$axisPen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(55,65,81),2)
$gridPen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(220,225,230),1)
$blue = [Drawing.Pen]::new([Drawing.Color]::FromArgb(35,99,170),3)
$green = [Drawing.Pen]::new([Drawing.Color]::FromArgb(35,135,110),3)
try {
    $bitmap = [Drawing.Bitmap]::new(1400,680)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.Clear([Drawing.Color]::White)
        $g.DrawString('Synthetic sensor observations (not real measurements)',$titleFont,[Drawing.Brushes]::Black,75,20)
        $g.DrawString('Blue: observation    Green: known baseline    Red: target anomaly',$font,[Drawing.Brushes]::Black,100,75)
        foreach ($v in 17..25) {
            $y = [single](570-($v-17)*52)
            $g.DrawLine($gridPen,100,$y,1320,$y)
            $g.DrawString([string]$v,$font,[Drawing.Brushes]::Black,48,$y-14)
        }
        foreach ($v in @(1,20,40,60,80,100,120)) {
            $x = [single](100+($v-1)*1220/119)
            $g.DrawString([string]$v,$font,[Drawing.Brushes]::Black,$x-15,588)
        }
        $g.DrawLine($axisPen,100,150,100,570); $g.DrawLine($axisPen,100,570,1320,570)
        $g.DrawString('Sample index (1 second per sample)',$font,[Drawing.Brushes]::Black,480,631)
        for ($j=1; $j -lt $rows.Count; $j++) {
            $left = $rows[$j-1]; $right = $rows[$j]
            $x1=[single](100+($left.i-1)*1220/119); $x2=[single](100+($right.i-1)*1220/119)
            $g.DrawLine($blue,$x1,[single](570-($left.observed-17)*52),$x2,[single](570-($right.observed-17)*52))
            $g.DrawLine($green,$x1,[single](570-($left.baseline-17)*52),$x2,[single](570-($right.baseline-17)*52))
        }
        foreach ($r in $rows | Where-Object label -eq 1) {
            $g.FillEllipse([Drawing.Brushes]::Firebrick,[single](100+($r.i-1)*1220/119-5),[single](570-($r.observed-17)*52-5),10,10)
        }
        $bitmap.Save((Join-Path $PSScriptRoot 'figures/signal.png'),[Drawing.Imaging.ImageFormat]::Png)
    } finally { $g.Dispose(); $bitmap.Dispose() }
    $bitmap = [Drawing.Bitmap]::new(1400,680); $g = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.Clear([Drawing.Color]::White)
        $g.DrawString('Metric comparison on one synthetic sequence',$titleFont,[Drawing.Brushes]::Black,80,25)
        $names=@('Accuracy','Precision','Recall','F1')
        $brushes=@([Drawing.Brushes]::SteelBlue,[Drawing.Brushes]::SeaGreen,[Drawing.Brushes]::Goldenrod,[Drawing.Brushes]::IndianRed)
        for ($k=0; $k -lt 4; $k++) {
            $g.FillRectangle($brushes[$k],120+$k*290,90,22,22)
            $g.DrawString($names[$k],$font,[Drawing.Brushes]::Black,152+$k*290,86)
        }
        for ($tick=0; $tick -le 5; $tick++) {
            $y=[single](570-$tick*80)
            $g.DrawLine($gridPen,100,$y,1320,$y)
            $g.DrawString((F ($tick/5.0) 1),$font,[Drawing.Brushes]::Black,43,$y-14)
        }
        for ($j=0; $j -lt 3; $j++) {
            for ($k=0; $k -lt 4; $k++) {
                $v=$stats[$j].($names[$k]); $x=165+$j*390+$k*72
                $g.FillRectangle($brushes[$k],$x,[single](570-$v*400),52,[single]($v*400))
                $g.DrawString((F $v 2),$font,[Drawing.Brushes]::Black,$x-4,[single](536-$v*400))
            }
            $g.DrawString(('Rule '+$stats[$j].model),$font,[Drawing.Brushes]::Black,235+$j*390,600)
        }
        $bitmap.Save((Join-Path $PSScriptRoot 'figures/metrics.png'),[Drawing.Imaging.ImageFormat]::Png)
    } finally { $g.Dispose(); $bitmap.Dispose() }
} finally { $font.Dispose(); $titleFont.Dispose(); $axisPen.Dispose(); $gridPen.Dispose(); $blue.Dispose(); $green.Dispose() }
$stats | Format-Table
Write-Host 'Generated 120 rows, four TeX tables, metric CSV, and two PNG figures.'
