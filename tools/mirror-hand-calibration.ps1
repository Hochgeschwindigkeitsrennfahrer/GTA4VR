param(
    [int]$GameProcessId=0,
    [string]$OutputPath='build/issue016-mirrored-hands.json'
)
$ErrorActionPreference='Stop'
$state=& (Join-Path $PSScriptRoot 'game-command.ps1') hand-status -GameProcessId $GameProcessId
if(-not $state.ok -or -not $state.hands.enabled) {throw 'Enabled hand IK is required.'}
$right=@($state.hands.correctionRows[1] | ForEach-Object {[double]$_})
if($right.Count -ne 9) {throw 'Expected right-hand 3x3 correction.'}
foreach($v in $right) {
    if([double]::IsNaN($v) -or [double]::IsInfinity($v)) {throw 'Non-finite correction.'}
}
# Row-vector mapping: left bone frame mirrors its Z, while the
# mapped OpenXR grip frame mirrors palm-normal X. Two reflections keep
# the result a proper rotation: Cleft = diag(1,1,-1) Cright diag(-1,1,1).
# The initial Y reflection left the Niko palm upside-down; a longitudinal
# X half-turn changes it to this Z reflection, accepted on the Niko rig.
# Authored left wrist conventions still need a visual check on each model.
$left=@(for($row=0;$row -lt 3;$row++) {
    for($col=0;$col -lt 3;$col++) {
        $rowSign=if($row -eq 2){-1.0}else{1.0}
        $colSign=if($col -eq 0){-1.0}else{1.0}
        $right[3*$row+$col]*$rowSign*$colSign
    }
})
function Convert-RowRotationToQuaternion([double[]]$r) {
    for($i=0;$i -lt 3;$i++) {
        for($j=0;$j -lt 3;$j++) {
            $dot=0.0
            for($k=0;$k -lt 3;$k++) {$dot+=$r[3*$i+$k]*$r[3*$j+$k]}
            $expected=if($i -eq $j){1.0}else{0.0}
            if([Math]::Abs($dot-$expected) -gt .001) {throw 'Correction is not orthonormal.'}
        }
    }
    $det=$r[0]*($r[4]*$r[8]-$r[5]*$r[7])-$r[1]*($r[3]*$r[8]-$r[5]*$r[6])+$r[2]*($r[3]*$r[7]-$r[4]*$r[6])
    if([Math]::Abs($det-1) -gt .001) {throw 'Correction is not a proper rotation.'}
    # Transpose row-vector matrix for the conventional quaternion formula.
    $m=@($r[0],$r[3],$r[6],$r[1],$r[4],$r[7],$r[2],$r[5],$r[8])
    $q=[double[]]::new(4)
    $trace=$m[0]+$m[4]+$m[8]
    if($trace -gt 0) {
        $s=2*[Math]::Sqrt(1+$trace)
        $q[0]=($m[7]-$m[5])/$s; $q[1]=($m[2]-$m[6])/$s
        $q[2]=($m[3]-$m[1])/$s; $q[3]=$s/4
    } else {
        $i=if($m[4] -gt $m[0]){1}else{0}
        if($m[8] -gt $m[3*$i+$i]) {$i=2}
        $j=($i+1)%3; $k=($i+2)%3
        $s=2*[Math]::Sqrt(1+$m[3*$i+$i]-$m[3*$j+$j]-$m[3*$k+$k])
        $q[$i]=$s/4; $q[$j]=($m[3*$j+$i]+$m[3*$i+$j])/$s
        $q[$k]=($m[3*$k+$i]+$m[3*$i+$k])/$s; $q[3]=($m[3*$k+$j]-$m[3*$j+$k])/$s
    }
    $norm=[Math]::Sqrt(($q | ForEach-Object {$_*$_} | Measure-Object -Sum).Sum)
    @($q | ForEach-Object {($_/$norm).ToString('F8',[Globalization.CultureInfo]::InvariantCulture)}) -join ' '
}
$result=[ordered]@{
    schema=1
    original=$state.hands
    leftRows=$left
    rightRows=$right
    leftQuaternion=(Convert-RowRotationToQuaternion $left)
    rightQuaternion=(Convert-RowRotationToQuaternion $right)
    leftVisuallyAccepted=$false
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding utf8
[pscustomobject]$result
